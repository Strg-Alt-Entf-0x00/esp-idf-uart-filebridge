"""Batch configuration schema using Pydantic for validation and JSON Schema generation."""

from __future__ import annotations

import os
from enum import Enum
from pathlib import Path
from typing import Any, Dict, List, Literal, Optional, Union

from pydantic import BaseModel, Field, field_validator, model_validator


class TaskType(str, Enum):
    """Supported batch task types."""

    UPLOAD = "upload"
    UPLOAD_DIR = "upload_dir"
    DOWNLOAD = "download"
    DOWNLOAD_DIR = "download_dir"
    DELETE = "delete"
    MKDIR = "mkdir"
    RENAME = "rename"
    COPY = "copy"
    CHECK_HASH = "check_hash"
    VERIFY_SIZE = "verify_size"
    LIST = "list"
    CHECK_SPACE = "check_space"


class ErrorPolicy(str, Enum):
    """Error handling strategies."""

    STOP = "stop"  # Stop on first error
    CONTINUE = "continue"  # Continue to next task
    RETRY = "retry"  # Retry failed task N times
    ROLLBACK = "rollback"  # Rollback changes on failure


class DeviceConfig(BaseModel):
    """Device connection configuration."""

    port: str = Field(
        ...,
        description="Serial port (e.g., COM13 on Windows, /dev/ttyUSB0 on Linux)",
        examples=["COM13", "/dev/ttyUSB0"],
    )
    baud: int = Field(
        default=3000000,
        description="Baud rate (default: 3,000,000)",
        ge=9600,
        le=3000000,
    )
    timeout: int = Field(
        default=120,
        description="Connection timeout in seconds",
        ge=1,
        le=300,
    )

    @field_validator("port")
    @classmethod
    def validate_port(cls, v: str) -> str:
        """Validate port format."""
        if not v:
            raise ValueError("Port cannot be empty")
        return v


class ConditionalCheck(BaseModel):
    """Conditional execution checks."""

    file_exists: Optional[str] = Field(
        default=None,
        description="Execute task only if remote file exists",
    )
    file_not_exists: Optional[str] = Field(
        default=None,
        description="Execute task only if remote file does not exist",
    )
    hash_matches: Optional[str] = Field(
        default=None,
        description="Execute task only if remote file hash matches (hex CRC32)",
    )
    hash_differs: Optional[str] = Field(
        default=None,
        description="Execute task only if remote file hash differs (hex CRC32)",
    )
    space_available: Optional[int] = Field(
        default=None,
        description="Execute task only if at least N bytes available",
        ge=0,
    )

    @model_validator(mode="after")
    def check_mutually_exclusive(self) -> ConditionalCheck:
        """Ensure mutually exclusive conditions."""
        if self.file_exists and self.file_not_exists:
            raise ValueError("file_exists and file_not_exists are mutually exclusive")
        if self.hash_matches and self.hash_differs:
            raise ValueError("hash_matches and hash_differs are mutually exclusive")
        return self


class RetryPolicy(BaseModel):
    """Retry configuration for tasks."""

    max_attempts: int = Field(
        default=3,
        description="Maximum retry attempts",
        ge=1,
        le=10,
    )
    backoff_seconds: float = Field(
        default=1.0,
        description="Delay between retries in seconds",
        ge=0.1,
        le=60.0,
    )
    exponential_backoff: bool = Field(
        default=False,
        description="Use exponential backoff (2^attempt * backoff_seconds)",
    )


class TaskBase(BaseModel):
    """Base task configuration."""

    name: str = Field(
        ...,
        description="Task name (for logging and progress tracking)",
        min_length=1,
        max_length=100,
    )
    type: TaskType = Field(
        ...,
        description="Task type",
    )
    condition: Optional[ConditionalCheck] = Field(
        default=None,
        description="Conditional execution rules",
    )
    retry: Optional[RetryPolicy] = Field(
        default=None,
        description="Retry policy (overrides global default)",
    )
    enabled: bool = Field(
        default=True,
        description="Enable/disable task execution",
    )


class UploadTask(TaskBase):
    """Upload single file task."""

    type: Literal[TaskType.UPLOAD] = TaskType.UPLOAD
    source: str = Field(
        ...,
        description="Local file path (supports ${VAR} substitution)",
    )
    destination: str = Field(
        ...,
        description="Remote file path",
    )
    verify: bool = Field(
        default=True,
        description="Verify CRC32 hash after upload",
    )
    overwrite: bool = Field(
        default=True,
        description="Overwrite if file exists",
    )

    @field_validator("source")
    @classmethod
    def validate_source(cls, v: str) -> str:
        """Validate source path (after variable substitution in runner)."""
        # Note: Actual file existence check happens at runtime after substitution
        return v

    @field_validator("destination")
    @classmethod
    def validate_destination(cls, v: str) -> str:
        """Validate destination path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        return v


class UploadDirTask(TaskBase):
    """Upload directory recursively."""

    type: Literal[TaskType.UPLOAD_DIR] = TaskType.UPLOAD_DIR
    source: str = Field(
        ...,
        description="Local directory path",
    )
    destination: str = Field(
        ...,
        description="Remote directory path",
    )
    pattern: Optional[str] = Field(
        default=None,
        description="File pattern filter (e.g., '*.bin', '*.wav')",
    )
    exclude: Optional[List[str]] = Field(
        default=None,
        description="Exclude patterns (e.g., ['*.tmp', '__pycache__'])",
    )
    verify: bool = Field(
        default=False,
        description="Verify CRC32 for each file (slow for large dirs)",
    )
    overwrite: bool = Field(
        default=True,
        description="Overwrite existing files",
    )
    create_dirs: bool = Field(
        default=True,
        description="Automatically create destination directories",
    )

    @field_validator("destination")
    @classmethod
    def validate_destination(cls, v: str) -> str:
        """Validate destination path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        return v


class DownloadTask(TaskBase):
    """Download single file task."""

    type: Literal[TaskType.DOWNLOAD] = TaskType.DOWNLOAD
    source: str = Field(
        ...,
        description="Remote file path",
    )
    destination: str = Field(
        ...,
        description="Local file path",
    )
    overwrite: bool = Field(
        default=True,
        description="Overwrite local file if exists",
    )

    @field_validator("source")
    @classmethod
    def validate_source(cls, v: str) -> str:
        """Validate source path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        return v


class DeleteTask(TaskBase):
    """Delete file or directory task."""

    type: Literal[TaskType.DELETE] = TaskType.DELETE
    path: str = Field(
        ...,
        description="Remote path to delete",
    )
    recursive: bool = Field(
        default=False,
        description="Delete directory recursively",
    )
    ignore_missing: bool = Field(
        default=False,
        description="Don't fail if file/directory doesn't exist",
    )

    @field_validator("path")
    @classmethod
    def validate_path(cls, v: str) -> str:
        """Validate path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        if v in ("/", "/sd", "/sd/"):
            raise ValueError("Cannot delete root or mount point")
        return v


class MkdirTask(TaskBase):
    """Create directory task."""

    type: Literal[TaskType.MKDIR] = TaskType.MKDIR
    path: str = Field(
        ...,
        description="Remote directory path to create",
    )
    parents: bool = Field(
        default=True,
        description="Create parent directories if needed",
    )
    ignore_exists: bool = Field(
        default=True,
        description="Don't fail if directory already exists",
    )

    @field_validator("path")
    @classmethod
    def validate_path(cls, v: str) -> str:
        """Validate path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        return v


class RenameTask(TaskBase):
    """Rename/move file or directory task."""

    type: Literal[TaskType.RENAME] = TaskType.RENAME
    old_path: str = Field(
        ...,
        description="Current remote path",
    )
    new_path: str = Field(
        ...,
        description="New remote path",
    )

    @field_validator("old_path", "new_path")
    @classmethod
    def validate_paths(cls, v: str) -> str:
        """Validate path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        return v


class CopyTask(TaskBase):
    """Copy file task (on remote device)."""

    type: Literal[TaskType.COPY] = TaskType.COPY
    source: str = Field(
        ...,
        description="Source remote path",
    )
    destination: str = Field(
        ...,
        description="Destination remote path",
    )

    @field_validator("source", "destination")
    @classmethod
    def validate_paths(cls, v: str) -> str:
        """Validate path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        return v


class CheckHashTask(TaskBase):
    """Verify file CRC32 hash task."""

    type: Literal[TaskType.CHECK_HASH] = TaskType.CHECK_HASH
    path: str = Field(
        ...,
        description="Remote file path",
    )
    expected_hash: str = Field(
        ...,
        description="Expected CRC32 hash (hex format, e.g., '0x12345678')",
        pattern=r"^0x[0-9a-fA-F]{8}$",
    )

    @field_validator("path")
    @classmethod
    def validate_path(cls, v: str) -> str:
        """Validate path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        return v


class VerifySizeTask(TaskBase):
    """Verify file size task."""

    type: Literal[TaskType.VERIFY_SIZE] = TaskType.VERIFY_SIZE
    path: str = Field(
        ...,
        description="Remote file path",
    )
    expected_size: int = Field(
        ...,
        description="Expected file size in bytes",
        ge=0,
    )

    @field_validator("path")
    @classmethod
    def validate_path(cls, v: str) -> str:
        """Validate path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        return v


class ListTask(TaskBase):
    """List directory contents task."""

    type: Literal[TaskType.LIST] = TaskType.LIST
    path: str = Field(
        default="/sd/",
        description="Remote directory path",
    )
    recursive: bool = Field(
        default=False,
        description="List recursively",
    )

    @field_validator("path")
    @classmethod
    def validate_path(cls, v: str) -> str:
        """Validate path."""
        if not v.startswith("/"):
            raise ValueError("Remote path must be absolute (start with /)")
        return v


class CheckSpaceTask(TaskBase):
    """Check available space task."""

    type: Literal[TaskType.CHECK_SPACE] = TaskType.CHECK_SPACE
    required_bytes: int = Field(
        ...,
        description="Required free space in bytes",
        ge=0,
    )
    mount_point: str = Field(
        default="/sd",
        description="Mount point to check",
    )


# Union of all task types for discriminated union
Task = Union[
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
]


class GlobalOptions(BaseModel):
    """Global batch execution options."""

    dry_run: bool = Field(
        default=False,
        description="Simulate execution without making changes",
    )
    verbose: bool = Field(
        default=False,
        description="Enable verbose logging",
    )
    progress_bar: bool = Field(
        default=True,
        description="Show progress bars for file transfers",
    )
    fail_fast: bool = Field(
        default=True,
        description="Stop on first error (equivalent to error_policy='stop')",
    )
    error_policy: ErrorPolicy = Field(
        default=ErrorPolicy.STOP,
        description="Global error handling strategy",
    )
    default_retry: Optional[RetryPolicy] = Field(
        default=None,
        description="Default retry policy for all tasks",
    )
    state_file: Optional[str] = Field(
        default=None,
        description="Path to state file for resume support (auto-generated if not set)",
    )


class BatchConfig(BaseModel):
    """Complete batch configuration."""

    version: str = Field(
        default="1.0",
        description="Config version",
        pattern=r"^\d+\.\d+$",
    )
    description: Optional[str] = Field(
        default=None,
        description="Batch job description",
        max_length=500,
    )
    device: DeviceConfig = Field(
        ...,
        description="Device connection settings",
    )
    variables: Optional[Dict[str, Any]] = Field(
        default=None,
        description="Variable definitions for ${VAR} substitution",
    )
    options: Optional[GlobalOptions] = Field(
        default_factory=GlobalOptions,
        description="Global execution options",
    )
    tasks: List[Task] = Field(
        ...,
        description="List of tasks to execute",
        min_length=1,
    )

    @model_validator(mode="after")
    def validate_tasks(self) -> BatchConfig:
        """Additional task validation."""
        # Check for duplicate task names
        names = [task.name for task in self.tasks if task.enabled]
        duplicates = [name for name in names if names.count(name) > 1]
        if duplicates:
            raise ValueError(f"Duplicate task names: {set(duplicates)}")
        return self

    def model_dump_json_schema(self) -> str:
        """Generate JSON Schema for IDE support."""
        return self.model_json_schema()


# Export JSON Schema for IDE autocomplete
if __name__ == "__main__":
    import json

    schema = BatchConfig.model_json_schema()
    print(json.dumps(schema, indent=2))
