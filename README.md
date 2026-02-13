# ImageCurry

A production-ready HTTP file server with CORS support, written in C.

## Features

- **HTTP Methods**: GET, POST, HEAD, OPTIONS
- **CORS Support**: Full CORS with wildcard origin (`Access-Control-Allow-Origin: *`)
- **Security**: Path traversal prevention, input validation, file size limits
- **Caching**: ETag and Last-Modified headers for efficient client caching
- **Logging**: Comprehensive request/response logging
- **Background Processing**: Automatic WebP compression for uploaded images
- **Separate Directories**:
  - `serve/`: Public files served via GET/HEAD
  - `save/`: Uploaded files stored via POST

## Building

Compile the server:

```bash
bash a.sh
```

This generates the executable `a`.

## Usage

Start the server:

```bash
./a
```

The server will:
- Run on `http://localhost:8080`
- Create `./serve` and `./save` directories if they don't exist
- Log requests to `./server.log`

Stop the server with `Ctrl+C`.

## API Reference

### GET `/` - Download File

Query parameter: `name` - filename to download from `./serve/`

```bash
curl "http://localhost:8080/?name=image.jpg"
```

Response headers include:
- `Last-Modified`
- `ETag`
- `Cache-Control: public, max-age=31536000, immutable`

### POST `/` - Upload File

Query parameter: `name` - filename for the uploaded file

```bash
curl -X POST "http://localhost:8080/?name=photo.jpg" --data-binary @photo.jpg
```

The uploaded file is saved to `./save/` and automatically compressed to WebP in `./serve/`.

### HEAD `/` - Get File Metadata

Query parameter: `name` - filename

```bash
curl -I "http://localhost:8080/?name=image.jpg"
```

Returns headers without the file body.

### OPTIONS `/` - CORS Preflight

```bash
curl -X OPTIONS "http://localhost:8080/?name=test.jpg"
```

Returns CORS headers for preflight requests.

## Directory Structure

```
.
├── main.c              # Server setup and request processing
├── handlers.c/h        # HTTP method handlers
├── http_response.c/h   # HTTP response helpers
├── logging.c/h         # Logging utilities
├── utils.c/h           # Utility functions
├── a.sh                # Build script
├── a                   # Compiled executable (after build)
├── serve/              # Public files (served via GET/HEAD)
├── save/               # Uploaded files (POST destination)
├── server.log          # Request/response log
└── compressor.sh       # WebP compression script
```

## Configuration

Constants defined in `main.c`:

```c
#define SERVER_PORT         8080
#define MAX_CONNECTIONS     128
#define MAX_FILE_SIZE       (128 * 1024 * 1024)  // 128MB
#define REQUEST_TIMEOUT     30
```

## Security

- Filename validation: alphanumeric, dots, underscores, dashes only
- Path traversal prevention: blocks `..`, `/`, `\`
- File size limits: 128MB max upload
- Request timeout: 30 seconds

## WebP Compression

Uploaded images are automatically compressed to WebP format in the background via `compressor.sh`. The WebP file is saved in `./serve/` with the same base name but `.webp` extension.

Example:
- Upload: `photo.jpg` → saved to `save/photo.jpg`
- Compress: `save/photo.jpg` → `serve/photo.webp`

## License

MIT License - see [LICENSE](LICENSE) for details.

## Author

ASAyman
