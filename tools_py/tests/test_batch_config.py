"""Tests for batch configuration validation."""

import pytest
from pydantic import ValidationError

from esp_idf_uart_filebridge.batch_config import (
    BatchConfig,
    DeviceConfig,
    UploadTask,
    UploadDirTask,
    DeleteTask,
    MkdirTask,
    TaskType,
    ErrorPolicy,
)


def test_minimal_config():
    """Test minimal valid configuration."""
    config = BatchConfig(
        device=DeviceConfig(port="COM13"),
        tasks=[
            UploadTask(
                name="Upload file",
                type=TaskType.UPLOAD,
                source="./local.bin",
                destination="/sd/remote.bin",
            )
        ],
    )
    assert config.version == "1.0"
    assert config.device.port == "COM13"
    assert config.device.baud == 3000000  # Default
    assert len(config.tasks) == 1


def test_device_config_validation():
    """Test device configuration validation."""
    # Valid config
    device = DeviceConfig(port="COM13", baud=115200)
    assert device.port == "COM13"
    assert device.baud == 115200

    # Invalid baud rate (too low)
    with pytest.raises(ValidationError):
        DeviceConfig(port="COM13", baud=1200)

    # Invalid baud rate (too high)
    with pytest.raises(ValidationError):
        DeviceConfig(port="COM13", baud=5000000)

    # Empty port
    with pytest.raises(ValidationError):
        DeviceConfig(port="")


def test_upload_task_validation():
    """Test upload task validation."""
    # Valid task
    task = UploadTask(
        name="Upload",
        source="./local.bin",
        destination="/sd/remote.bin",
    )
    assert task.type == TaskType.UPLOAD
    assert task.verify is True  # Default

    # Invalid destination (not absolute)
    with pytest.raises(ValidationError):
        UploadTask(
            name="Upload",
            source="./local.bin",
            destination="relative/path.bin",  # Must start with /
        )


def test_upload_dir_task_validation():
    """Test upload directory task validation."""
    # Valid task
    task = UploadDirTask(
        name="Upload dir",
        source="./local_dir/",
        destination="/sd/remote_dir/",
        pattern="*.wav",
        exclude=["*.tmp"],
    )
    assert task.type == TaskType.UPLOAD_DIR
    assert task.pattern == "*.wav"
    assert task.exclude == ["*.tmp"]

    # Invalid destination
    with pytest.raises(ValidationError):
        UploadDirTask(
            name="Upload dir",
            source="./local_dir/",
            destination="relative/path/",  # Must start with /
        )


def test_delete_task_validation():
    """Test delete task validation."""
    # Valid task
    task = DeleteTask(
        name="Delete",
        path="/sd/test/",
        recursive=True,
    )
    assert task.type == TaskType.DELETE
    assert task.recursive is True

    # Invalid path (root deletion)
    with pytest.raises(ValidationError):
        DeleteTask(name="Delete", path="/")

    with pytest.raises(ValidationError):
        DeleteTask(name="Delete", path="/sd")


def test_mkdir_task_validation():
    """Test mkdir task validation."""
    # Valid task
    task = MkdirTask(
        name="Create dir",
        path="/sd/new_dir",
        parents=True,
    )
    assert task.type == TaskType.MKDIR
    assert task.parents is True

    # Invalid path (not absolute)
    with pytest.raises(ValidationError):
        MkdirTask(name="Create dir", path="relative/path")


def test_variable_substitution():
    """Test variable substitution in config."""
    config = BatchConfig(
        device=DeviceConfig(port="COM13"),
        variables={
            "project_root": "/home/user/project",
            "lang": "en",
        },
        tasks=[
            UploadTask(
                name="Upload",
                source="${project_root}/models/model.bin",
                destination="/sd/${lang}/model.bin",
            )
        ],
    )
    assert config.variables["project_root"] == "/home/user/project"
    assert config.variables["lang"] == "en"


def test_conditional_check_validation():
    """Test conditional check validation."""
    from esp_idf_uart_filebridge.batch_config import ConditionalCheck

    # Valid conditions
    cond1 = ConditionalCheck(file_exists="/sd/file.bin")
    assert cond1.file_exists == "/sd/file.bin"

    cond2 = ConditionalCheck(hash_matches="0x12345678")
    assert cond2.hash_matches == "0x12345678"

    # Mutually exclusive conditions
    with pytest.raises(ValidationError):
        ConditionalCheck(
            file_exists="/sd/file.bin",
            file_not_exists="/sd/file.bin",  # Mutually exclusive
        )

    with pytest.raises(ValidationError):
        ConditionalCheck(
            hash_matches="0x12345678",
            hash_differs="0xABCDEF00",  # Mutually exclusive
        )


def test_retry_policy():
    """Test retry policy configuration."""
    from esp_idf_uart_filebridge.batch_config import RetryPolicy

    # Valid retry policy
    retry = RetryPolicy(
        max_attempts=5, backoff_seconds=2.0, exponential_backoff=True
    )
    assert retry.max_attempts == 5
    assert retry.backoff_seconds == 2.0
    assert retry.exponential_backoff is True

    # Invalid max_attempts (too high)
    with pytest.raises(ValidationError):
        RetryPolicy(max_attempts=100)

    # Invalid backoff (too high)
    with pytest.raises(ValidationError):
        RetryPolicy(backoff_seconds=1000.0)


def test_global_options():
    """Test global options configuration."""
    from esp_idf_uart_filebridge.batch_config import GlobalOptions

    # Valid options
    options = GlobalOptions(
        dry_run=True,
        verbose=True,
        fail_fast=False,
        error_policy=ErrorPolicy.CONTINUE,
    )
    assert options.dry_run is True
    assert options.verbose is True
    assert options.fail_fast is False
    assert options.error_policy == ErrorPolicy.CONTINUE


def test_duplicate_task_names():
    """Test detection of duplicate task names."""
    with pytest.raises(ValidationError, match="Duplicate task names"):
        BatchConfig(
            device=DeviceConfig(port="COM13"),
            tasks=[
                UploadTask(
                    name="Upload",  # Duplicate name
                    source="./file1.bin",
                    destination="/sd/file1.bin",
                ),
                UploadTask(
                    name="Upload",  # Duplicate name
                    source="./file2.bin",
                    destination="/sd/file2.bin",
                ),
            ],
        )


def test_task_enabled_flag():
    """Test task enabled flag."""
    config = BatchConfig(
        device=DeviceConfig(port="COM13"),
        tasks=[
            UploadTask(
                name="Upload 1",
                source="./file1.bin",
                destination="/sd/file1.bin",
                enabled=True,
            ),
            UploadTask(
                name="Upload 2",
                source="./file2.bin",
                destination="/sd/file2.bin",
                enabled=False,  # Disabled
            ),
        ],
    )
    assert config.tasks[0].enabled is True
    assert config.tasks[1].enabled is False


def test_json_serialization():
    """Test JSON serialization/deserialization."""
    config_dict = {
        "version": "1.0",
        "device": {"port": "COM13", "baud": 3000000},
        "tasks": [
            {
                "name": "Upload",
                "type": "upload",
                "source": "./local.bin",
                "destination": "/sd/remote.bin",
            }
        ],
    }

    # Deserialize
    config = BatchConfig(**config_dict)
    assert config.device.port == "COM13"
    assert len(config.tasks) == 1

    # Serialize
    serialized = config.model_dump()
    assert serialized["device"]["port"] == "COM13"
    assert serialized["tasks"][0]["name"] == "Upload"


def test_complex_config():
    """Test complex configuration with multiple task types."""
    config = BatchConfig(
        version="1.0",
        description="Complex batch job",
        device=DeviceConfig(port="COM13", baud=921600),
        variables={"project": "/home/user/project", "lang": "en"},
        options={
            "verbose": True,
            "progress_bar": True,
            "fail_fast": False,
            "default_retry": {
                "max_attempts": 3,
                "backoff_seconds": 2.0,
                "exponential_backoff": True,
            },
        },
        tasks=[
            {
                "name": "Check space",
                "type": "check_space",
                "required_bytes": 10485760,
            },
            {"name": "Create dir", "type": "mkdir", "path": "/sd/models"},
            {
                "name": "Upload file",
                "type": "upload",
                "source": "${project}/model.bin",
                "destination": "/sd/models/model.bin",
                "verify": True,
                "condition": {"file_not_exists": "/sd/models/model.bin"},
            },
            {
                "name": "Upload dir",
                "type": "upload_dir",
                "source": "${project}/data/",
                "destination": "/sd/data/",
                "pattern": "*.wav",
                "exclude": ["*.tmp"],
            },
            {
                "name": "List files",
                "type": "list",
                "path": "/sd/models/",
            },
        ],
    )

    assert config.description == "Complex batch job"
    assert len(config.tasks) == 5
    assert config.options.verbose is True
    assert config.variables["project"] == "/home/user/project"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
