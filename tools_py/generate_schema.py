#!/usr/bin/env python3
"""Generate JSON Schema for batch configuration files.

This script generates a JSON Schema that can be used by IDEs
for autocomplete and validation of batch configuration files.

Usage:
    python generate_schema.py > batch_config_schema.json
"""

import json
from esp_idf_uart_filebridge.batch_config import BatchConfig


def main():
    """Generate and print JSON Schema."""
    schema = BatchConfig.model_json_schema()
    
    # Add schema metadata
    schema["$schema"] = "http://json-schema.org/draft-07/schema#"
    schema["title"] = "ESP-IDF UART FileBridge Batch Configuration"
    schema["description"] = (
        "Configuration schema for batch file transfer workflows. "
        "Supports declarative task definitions with validation, retry policies, "
        "conditional execution, and progress tracking."
    )
    
    print(json.dumps(schema, indent=2))


if __name__ == "__main__":
    main()
