# SquirrelDB C SDK

Official C client for SquirrelDB.

## Installation

Download the latest release from [GitHub Releases](https://github.com/sqrldb/sdk-c/releases) or build from source:

```bash
git clone https://github.com/sqrldb/sdk-c.git
cd sdk-c
make
sudo make install
```

## Quick Start

```c
#include <squirreldb.h>
#include <stdio.h>

int main() {
    // Connect to database
    sqrl_config_t config = {
        .host = "localhost",
        .port = 8080,
        .token = getenv("SQUIRRELDB_TOKEN")
    };

    sqrl_client_t *client = sqrl_connect(&config);
    if (!client) {
        fprintf(stderr, "Failed to connect\n");
        return 1;
    }

    // Insert a document
    const char *doc = "{\"name\": \"Alice\", \"email\": \"alice@example.com\"}";
    sqrl_result_t *result = sqrl_insert(client, "users", doc);

    if (result) {
        printf("Created user: %s\n", sqrl_result_get_id(result));
        sqrl_result_free(result);
    }

    // Query documents
    sqrl_result_t *users = sqrl_query(client, "users", "u => u.status === 'active'");

    // Cleanup
    sqrl_disconnect(client);
    return 0;
}
```

## Documentation

Visit [squirreldb.com/docs/sdks](https://squirreldb.com/docs/sdks) for full documentation.

## License

Apache License 2.0 - see [LICENSE](LICENSE) for details.
