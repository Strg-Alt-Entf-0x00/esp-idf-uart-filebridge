"""Batch execution engine for esp-idf-uart-filebridge."""

from __future__ import annotations

import json
import logging
import os
import re
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

from rich.console import Console
from rich.progress import (
    Progress,
    SpinnerColumn,
    TextColumn,
    BarColumn,
    TaskProgressColumn,
    TimeRemainingColumn,
)
from rich.table import Table
from rich.panel import Panel

from .batch_config import (
    BatchConfig,
    Task,
    TaskType,
    ErrorPolicy,
    ConditionalCheck,
    UploadTask,
    UploadDirTask,
    DownloadTask,
    DeleteTask,
    MkdirTask,
    RenameTask,
    CopyTask,
    CheckHashTask,
    VerifySizeTask,
    ListTask,
    CheckSpaceTask,
)
from .file_manager import ESP32FileManager
from .protocol import ESP32ProtocolError, ERR_FILE_NOT_FOUND, ERR_FILE_EXISTS

logger = logging.getLogger(__name__)


class TaskResult:
    """Result of a single task execution."""

    def __init__(
        self,
        task_name: str,
        success: bool,
        message: str = "",
        skipped: bool = False,
        error: Optional[Exception] = None,
    ):
        self.task_name = task_name
        self.success = success
        self.message = message
        self.skipped = skipped
        self.error = error
        self.timestamp = time.time()


class BatchState:
    """Track batch execution state for resume support."""

    def __init__(self, state_file: Optional[str] = None):
        self.state_file = state_file or ".batch_state.json"
        self.completed_tasks: List[str] = []
        self.failed_tasks: List[str] = []
        self.start_time: Optional[float] = None
        self.end_time: Optional[float] = None

    def load(self) -> None:
        """Load state from file."""
        if os.path.exists(self.state_file):
            try:
                with open(self.state_file, "r") as f:
                    data = json.load(f)
                    self.completed_tasks = data.get("completed_tasks", [])
                    self.failed_tasks = data.get("failed_tasks", [])
                    self.start_time = data.get("start_time")
                    self.end_time = data.get("end_time")
                logger.info(f"Loaded state from {self.state_file}")
            except Exception as e:
                logger.warning(f"Failed to load state file: {e}")

    def save(self) -> None:
        """Save state to file."""
        try:
            with open(self.state_file, "w") as f:
                json.dump(
                    {
                        "completed_tasks": self.completed_tasks,
                        "failed_tasks": self.failed_tasks,
                        "start_time": self.start_time,
                        "end_time": self.end_time,
                    },
                    f,
                    indent=2,
                )
        except Exception as e:
            logger.warning(f"Failed to save state file: {e}")

    def mark_completed(self, task_name: str) -> None:
        """Mark task as completed."""
        if task_name not in self.completed_tasks:
            self.completed_tasks.append(task_name)
        self.save()

    def mark_failed(self, task_name: str) -> None:
        """Mark task as failed."""
        if task_name not in self.failed_tasks:
            self.failed_tasks.append(task_name)
        self.save()

    def is_completed(self, task_name: str) -> bool:
        """Check if task is completed."""
        return task_name in self.completed_tasks

    def clear(self) -> None:
        """Clear state file."""
        if os.path.exists(self.state_file):
            os.remove(self.state_file)


class BatchRunner:
    """Execute batch configuration."""

    def __init__(self, config: BatchConfig, console: Optional[Console] = None):
        self.config = config
        self.console = console or Console()
        self.state = BatchState(config.options.state_file)
        self.file_manager: Optional[ESP32FileManager] = None
        self.results: List[TaskResult] = []

    def substitute_variables(self, text: str) -> str:
        """Substitute ${VAR} placeholders with values from config.variables."""
        if not self.config.variables:
            return text

        def replacer(match):
            var_name = match.group(1)
            if var_name in self.config.variables:
                return str(self.config.variables[var_name])
            else:
                logger.warning(f"Variable ${{{var_name}}} not defined, keeping as-is")
                return match.group(0)

        return re.sub(r"\$\{(\w+)\}", replacer, text)

    def connect(self) -> bool:
        """Connect to ESP32 device."""
        try:
            self.file_manager = ESP32FileManager(
                self.config.device.port, self.config.device.baud
            )
            if not self.file_manager.connect():
                raise ESP32ProtocolError("Failed to connect to device")

            info = self.file_manager.get_device_info()
            self.console.print(
                Panel(
                    f"[green]Connected to {info.device_name}[/green]\n"
                    f"FW: {info.fw_version} | SD: {'Yes' if info.sd_present else 'No'}",
                    title="Device Info",
                )
            )
            return True
        except Exception as e:
            self.console.print(f"[red]Connection failed: {e}[/red]")
            return False

    def disconnect(self) -> None:
        """Disconnect from device."""
        if self.file_manager:
            self.file_manager.disconnect()

    def check_condition(self, condition: Optional[ConditionalCheck]) -> bool:
        """Check if task condition is met."""
        if not condition:
            return True

        try:
            # File existence checks
            if condition.file_exists:
                path = self.substitute_variables(condition.file_exists)
                try:
                    self.file_manager.get_file_stat(path)
                    return True
                except ESP32ProtocolError:
                    return False

            if condition.file_not_exists:
                path = self.substitute_variables(condition.file_not_exists)
                try:
                    self.file_manager.get_file_stat(path)
                    return False
                except ESP32ProtocolError:
                    return True

            # Hash checks
            if condition.hash_matches or condition.hash_differs:
                path = self.substitute_variables(
                    condition.hash_matches or condition.hash_differs
                )
                try:
                    actual_hash = self.file_manager.proto.hash_file(path)
                    expected_hash = int(
                        condition.hash_matches or condition.hash_differs, 16
                    )
                    matches = actual_hash == expected_hash
                    return matches if condition.hash_matches else not matches
                except ESP32ProtocolError:
                    return False

            # Space check
            if condition.space_available:
                info = self.file_manager.proto.get_space_info()
                return info["free_bytes"] >= condition.space_available

            return True

        except Exception as e:
            logger.warning(f"Condition check failed: {e}")
            return False

    def execute_task_with_retry(self, task: Task) -> TaskResult:
        """Execute task with retry policy."""
        retry_policy = task.retry or self.config.options.default_retry
        max_attempts = retry_policy.max_attempts if retry_policy else 1
        backoff = retry_policy.backoff_seconds if retry_policy else 1.0
        exponential = retry_policy.exponential_backoff if retry_policy else False

        for attempt in range(1, max_attempts + 1):
            try:
                result = self.execute_single_task(task)
                if result.success:
                    return result

                if attempt < max_attempts:
                    delay = backoff * (2 ** (attempt - 1) if exponential else 1)
                    self.console.print(
                        f"[yellow]Task '{task.name}' failed (attempt {attempt}/{max_attempts}), "
                        f"retrying in {delay:.1f}s...[/yellow]"
                    )
                    time.sleep(delay)
                else:
                    return result

            except Exception as e:
                if attempt < max_attempts:
                    delay = backoff * (2 ** (attempt - 1) if exponential else 1)
                    self.console.print(
                        f"[yellow]Task '{task.name}' error (attempt {attempt}/{max_attempts}): {e}, "
                        f"retrying in {delay:.1f}s...[/yellow]"
                    )
                    time.sleep(delay)
                else:
                    return TaskResult(task.name, False, f"Failed after {max_attempts} attempts", error=e)

        return TaskResult(task.name, False, f"Failed after {max_attempts} attempts")

    def execute_single_task(self, task: Task) -> TaskResult:
        """Execute a single task based on its type."""
        # Check condition
        if not self.check_condition(task.condition):
            return TaskResult(task.name, True, "Skipped (condition not met)", skipped=True)

        # Dry-run mode
        if self.config.options.dry_run:
            return TaskResult(task.name, True, "Dry-run (not executed)", skipped=True)

        # Execute based on type
        try:
            if task.type == TaskType.UPLOAD:
                return self._execute_upload(task)
            elif task.type == TaskType.UPLOAD_DIR:
                return self._execute_upload_dir(task)
            elif task.type == TaskType.DOWNLOAD:
                return self._execute_download(task)
            elif task.type == TaskType.DELETE:
                return self._execute_delete(task)
            elif task.type == TaskType.MKDIR:
                return self._execute_mkdir(task)
            elif task.type == TaskType.RENAME:
                return self._execute_rename(task)
            elif task.type == TaskType.COPY:
                return self._execute_copy(task)
            elif task.type == TaskType.CHECK_HASH:
                return self._execute_check_hash(task)
            elif task.type == TaskType.VERIFY_SIZE:
                return self._execute_verify_size(task)
            elif task.type == TaskType.LIST:
                return self._execute_list(task)
            elif task.type == TaskType.CHECK_SPACE:
                return self._execute_check_space(task)
            else:
                return TaskResult(task.name, False, f"Unknown task type: {task.type}")

        except Exception as e:
            logger.exception(f"Task '{task.name}' failed with exception")
            return TaskResult(task.name, False, str(e), error=e)

    def _execute_upload(self, task: UploadTask) -> TaskResult:
        """Execute upload task."""
        source = Path(self.substitute_variables(task.source))
        destination = self.substitute_variables(task.destination)

        if not source.exists():
            return TaskResult(task.name, False, f"Source file not found: {source}")

        if not task.overwrite:
            try:
                self.file_manager.get_file_stat(destination)
                return TaskResult(task.name, True, "File exists, skipping", skipped=True)
            except ESP32ProtocolError:
                pass  # File doesn't exist, proceed

        self.file_manager.upload_file(source, destination)

        if task.verify:
            local_crc = self.file_manager.proto.compute_crc32_file(source)
            remote_crc = self.file_manager.proto.hash_file(destination)
            if local_crc != remote_crc:
                return TaskResult(task.name, False, "CRC32 verification failed")

        return TaskResult(task.name, True, f"Uploaded {source.name}")

    def _execute_upload_dir(self, task: UploadDirTask) -> TaskResult:
        """Execute upload directory task."""
        source = Path(self.substitute_variables(task.source))
        destination = self.substitute_variables(task.destination)

        if not source.is_dir():
            return TaskResult(task.name, False, f"Source directory not found: {source}")

        # Collect files to upload
        files_to_upload = []
        for root, dirs, files in os.walk(source):
            for file in files:
                local_path = Path(root) / file
                rel_path = local_path.relative_to(source)

                # Apply pattern filter
                if task.pattern and not local_path.match(task.pattern):
                    continue

                # Apply exclude filter
                if task.exclude:
                    excluded = False
                    for exclude_pattern in task.exclude:
                        if local_path.match(exclude_pattern):
                            excluded = True
                            break
                    if excluded:
                        continue

                remote_path = f"{destination}/{rel_path.as_posix()}"
                files_to_upload.append((local_path, remote_path))

        if not files_to_upload:
            return TaskResult(task.name, True, "No files to upload", skipped=True)

        # Upload files with progress bar
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            TaskProgressColumn(),
            TimeRemainingColumn(),
            console=self.console,
            disable=not self.config.options.progress_bar,
        ) as progress:
            upload_task = progress.add_task(
                f"[cyan]{task.name}", total=len(files_to_upload)
            )

            for local_path, remote_path in files_to_upload:
                # Create parent directories
                if task.create_dirs:
                    parent_dir = os.path.dirname(remote_path)
                    try:
                        self.file_manager.proto.mkdir(parent_dir)
                    except ESP32ProtocolError:
                        pass  # Directory might already exist

                # Upload file
                self.file_manager.upload_file(local_path, remote_path)

                if task.verify:
                    local_crc = self.file_manager.proto.compute_crc32_file(local_path)
                    remote_crc = self.file_manager.proto.hash_file(remote_path)
                    if local_crc != remote_crc:
                        return TaskResult(task.name, False, f"CRC32 verification failed for {local_path.name}")

                progress.update(upload_task, advance=1)

        return TaskResult(task.name, True, f"Uploaded {len(files_to_upload)} files")

    def _execute_download(self, task: DownloadTask) -> TaskResult:
        """Execute download task."""
        source = self.substitute_variables(task.source)
        destination = Path(self.substitute_variables(task.destination))

        if destination.exists() and not task.overwrite:
            return TaskResult(task.name, True, "File exists, skipping", skipped=True)

        # Create parent directory
        destination.parent.mkdir(parents=True, exist_ok=True)

        self.file_manager.download_file(source, destination)
        return TaskResult(task.name, True, f"Downloaded {destination.name}")

    def _execute_delete(self, task: DeleteTask) -> TaskResult:
        """Execute delete task."""
        path = self.substitute_variables(task.path)

        try:
            self.file_manager.proto.delete_file_or_dir(path)
            return TaskResult(task.name, True, f"Deleted {path}")
        except ESP32ProtocolError as e:
            if task.ignore_missing and e.error_code == ERR_FILE_NOT_FOUND:
                return TaskResult(task.name, True, "File not found, ignoring", skipped=True)
            raise

    def _execute_mkdir(self, task: MkdirTask) -> TaskResult:
        """Execute mkdir task."""
        path = self.substitute_variables(task.path)

        try:
            self.file_manager.proto.mkdir(path)
            return TaskResult(task.name, True, f"Created directory {path}")
        except ESP32ProtocolError as e:
            if task.ignore_exists and e.error_code == ERR_FILE_EXISTS:
                return TaskResult(task.name, True, "Directory exists, ignoring", skipped=True)
            raise

    def _execute_rename(self, task: RenameTask) -> TaskResult:
        """Execute rename task."""
        old_path = self.substitute_variables(task.old_path)
        new_path = self.substitute_variables(task.new_path)

        self.file_manager.proto.rename_file_or_dir(old_path, new_path)
        return TaskResult(task.name, True, f"Renamed {old_path} to {new_path}")

    def _execute_copy(self, task: CopyTask) -> TaskResult:
        """Execute copy task."""
        source = self.substitute_variables(task.source)
        destination = self.substitute_variables(task.destination)

        self.file_manager.proto.copy_file(source, destination)
        return TaskResult(task.name, True, f"Copied {source} to {destination}")

    def _execute_check_hash(self, task: CheckHashTask) -> TaskResult:
        """Execute check hash task."""
        path = self.substitute_variables(task.path)
        expected_hash = int(task.expected_hash, 16)

        actual_hash = self.file_manager.proto.hash_file(path)
        if actual_hash == expected_hash:
            return TaskResult(task.name, True, f"Hash verified: {hex(actual_hash)}")
        else:
            return TaskResult(
                task.name,
                False,
                f"Hash mismatch: expected {hex(expected_hash)}, got {hex(actual_hash)}",
            )

    def _execute_verify_size(self, task: VerifySizeTask) -> TaskResult:
        """Execute verify size task."""
        path = self.substitute_variables(task.path)
        stat = self.file_manager.get_file_stat(path)

        if stat.size == task.expected_size:
            return TaskResult(task.name, True, f"Size verified: {stat.size} bytes")
        else:
            return TaskResult(
                task.name,
                False,
                f"Size mismatch: expected {task.expected_size}, got {stat.size}",
            )

    def _execute_list(self, task: ListTask) -> TaskResult:
        """Execute list task."""
        path = self.substitute_variables(task.path)
        entries = self.file_manager.list_directory(path, quiet=True)

        table = Table(title=f"Directory: {path}")
        table.add_column("Name", style="cyan")
        table.add_column("Type", style="yellow")
        table.add_column("Size", justify="right")

        for entry in sorted(entries, key=lambda e: e.name):
            table.add_row(
                entry.name,
                "[DIR]" if entry.is_directory else "[FILE]",
                "-" if entry.is_directory else f"{entry.size:,}",
            )

        self.console.print(table)
        return TaskResult(task.name, True, f"Listed {len(entries)} entries")

    def _execute_check_space(self, task: CheckSpaceTask) -> TaskResult:
        """Execute check space task."""
        info = self.file_manager.proto.get_space_info()
        free_bytes = info["free_bytes"]

        if free_bytes >= task.required_bytes:
            return TaskResult(
                task.name,
                True,
                f"Space available: {free_bytes:,} bytes (required: {task.required_bytes:,})",
            )
        else:
            return TaskResult(
                task.name,
                False,
                f"Insufficient space: {free_bytes:,} bytes available, {task.required_bytes:,} required",
            )

    def execute(self) -> bool:
        """Execute all tasks in batch."""
        self.console.print(
            Panel(
                f"[bold cyan]Batch: {self.config.description or 'Unnamed'}[/bold cyan]\n"
                f"Tasks: {len(self.config.tasks)} | Port: {self.config.device.port}",
                title="Starting Batch Execution",
            )
        )

        # Load state
        self.state.load()
        self.state.start_time = time.time()

        # Connect
        if not self.connect():
            return False

        try:
            # Execute tasks
            for task in self.config.tasks:
                if not task.enabled:
                    self.console.print(f"[dim]⊘ {task.name} (disabled)[/dim]")
                    continue

                # Check if already completed
                if self.state.is_completed(task.name):
                    self.console.print(f"[dim]✓ {task.name} (already completed)[/dim]")
                    continue

                self.console.print(f"\n[bold]▶ {task.name}[/bold] ({task.type.value})")

                result = self.execute_task_with_retry(task)
                self.results.append(result)

                if result.success:
                    if result.skipped:
                        self.console.print(f"[yellow]⊘ {result.message}[/yellow]")
                    else:
                        self.console.print(f"[green]✓ {result.message}[/green]")
                        self.state.mark_completed(task.name)
                else:
                    self.console.print(f"[red]✗ {result.message}[/red]")
                    self.state.mark_failed(task.name)

                    if self.config.options.fail_fast:
                        self.console.print("[red]Stopping (fail_fast enabled)[/red]")
                        break

        finally:
            self.disconnect()
            self.state.end_time = time.time()
            self.state.save()

        # Summary
        self._print_summary()

        return all(r.success for r in self.results if not r.skipped)

    def _print_summary(self) -> None:
        """Print execution summary."""
        total = len(self.results)
        successful = sum(1 for r in self.results if r.success and not r.skipped)
        skipped = sum(1 for r in self.results if r.skipped)
        failed = sum(1 for r in self.results if not r.success)

        duration = (self.state.end_time or time.time()) - (self.state.start_time or 0)

        summary_text = (
            f"Total: {total} | "
            f"[green]Success: {successful}[/green] | "
            f"[yellow]Skipped: {skipped}[/yellow] | "
            f"[red]Failed: {failed}[/red]\n"
            f"Duration: {duration:.1f}s"
        )

        self.console.print(
            Panel(summary_text, title="Execution Summary", border_style="bold")
        )
