# ImageCurry

A high-performance HTTP file server with automatic image compression, written in C++ with RAII-based memory management.

## Features

- **Automatic File Naming**: Upload files without needing to specify filenames - the server generates unique 64-character UUIDs
- **WebP Compression**: Automatic background compression of uploaded images to WebP format
- **CORS Support**: Full CORS with wildcard origin for cross-origin requests
- **Caching**: ETag and Last-Modified headers for efficient client-side caching
- **File Type Detection**: Automatic extension detection from Content-Type headers or magic bytes
- **Security**: Filename validation, path traversal prevention, file size limits
- **Logging**: Comprehensive request/response logging
- **Performance**: C++ with RAII for automatic resource management

## Building

### Prerequisites

- C++17 compatible compiler (g++ recommended)
- ImageMagick (for WebP compression via compressor.sh)
- Standard C++ libraries (no external dependencies for the server itself)

### Compilation

```bash
bash a.sh
```

This creates the executable `a` in the current directory.

## Usage

### Starting the Server

```bash
./a
```

The server will:
- Run on `http://localhost:8080`
- Create `./serve` and `./save` directories if they don't exist
- Log all requests to `./server.log`
- Listen for HTTP requests on port 8080

Stop the server with `Ctrl+C`.

### Output

```
HTTP File Server running on http://localhost:8080
Upload endpoint: POST /upload
Retrieve endpoint: GET/HEAD /retrieve?name=<filename>
Serve directory (GET/HEAD): ./serve
Save directory (POST): ./save
CORS: Enabled (Access-Control-Allow-Origin: *)
Press Ctrl+C to stop
```

## API Reference

### POST `/upload` - Upload File

Upload a file without specifying a filename. The server generates a unique UUID for you.

**Request:**
```bash
curl -X POST "http://localhost:8080/upload" \
    --data-binary @image.jpg \
    -H "Content-Type: image/jpeg"
```

**Response:**
```json
{
  "name": "18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c.webp"
}
```

**What happens:**
1. Server generates a unique 64-character UUID based on timestamp + random data
2. Original file is saved to `./save/` as `<uuid>.<extension>`
3. Background compression converts to WebP and saves to `./serve/` as `<uuid>.webp`
4. Response contains the WebP filename for retrieval

**File Type Detection:**
- If `Content-Type` header is provided, extension is derived from it
- If no header, extension is detected from file magic bytes:
  - JPEG: `.jpg`
  - PNG: `.png`
  - GIF: `.gif`
  - WebP: `.webp`
  - PDF: `.pdf`
  - ZIP: `.zip`
  - Other: `.bin`

### GET `/retrieve?name=<filename>` - Download File

Retrieve a compressed WebP image.

**Request:**
```bash
curl "http://localhost:8080/retrieve?name=18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c.webp"
```

**Response:**
- Returns the WebP image file
- Includes headers: ETag, Last-Modified, Content-Length, Content-Type

### HEAD `/retrieve?name=<filename>` - Get Metadata

Get file metadata without downloading the body.

**Request:**
```bash
curl -I "http://localhost:8080/retrieve?name=18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c.webp"
```

**Response:**
```
HTTP/1.1 200 OK
Access-Control-Allow-Origin: *
Content-Type: image/webp
Content-Length: 20742
Last-Modified: Wed, 18 Feb 2026 20:49:57 GMT
ETag: "1771447797-20742"
Cache-Control: public, max-age=31536000, immutable
Connection: close
```

### OPTIONS `/upload` or `/retrieve` - CORS Preflight

CORS preflight request for browser clients.

**Request:**
```bash
curl -X OPTIONS "http://localhost:8080/upload" -I
```

**Response:**
```
HTTP/1.1 204 No Content
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS
Access-Control-Allow-Headers: Content-Type, Content-Length, If-None-Match, If-Modified-Since, Authorization
Access-Control-Max-Age: 86400
```

## File Storage

### Directory Structure

```
.
├── serve/           # Compressed WebP images (for retrieval)
│   └── <uuid>.webp
├── save/            # Original uploaded files
│   └── <uuid>.<ext>
├── a                # Compiled executable
├── a.sh             # Build script
├── compressor.sh    # ImageMagick WebP compression
└── server.log       # Request/response logs
```

### File Naming

- **UUID Generation**: 64-character hexadecimal string
  - Based on: server timestamp (nanoseconds) + random data
  - Format: `timestamp(16hex) + random1(16hex) + random2(16hex) + random3(16hex) + hash(16hex)`
  - Example: `18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c`

- **Example Files:**
  - Original: `save/18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c.jpg`
  - Compressed: `serve/18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c.webp`

## WebP Compression

### Compression Settings

- **Tool**: ImageMagick (`convert` command)
- **Resolution**: Maximum 900x900 (maintains aspect ratio)
- **Quality**: 65%
- **Method**: 6 (good balance between size and quality)
- **Metadata**: Stripped (removes EXIF, etc.)

### Compression Ratios

Typical compression achieved:
- JPEG → WebP: 90-99% size reduction
- PNG → WebP: 70-95% size reduction

### Background Processing

Compression runs in a background process:
- No delay in API response
- 1-second delay before compression starts (to allow disk flush)
- Forked process with error logging

## Caching

### ETag Support

Client can include `If-None-Match` header:

```bash
curl "http://localhost:8080/retrieve?name=<uuid>.webp" \
    -H "If-None-Match: \"1771447797-20742\""
```

If ETag matches, returns `304 Not Modified` (no body transfer).

### Cache Headers

All retrieve responses include:
```
Cache-Control: public, max-age=31536000, immutable
```

Content is cacheable for 1 year and should not change.

## Error Responses

### 400 Bad Request
- Invalid path/endpoint
- Invalid filename
- Malformed request

### 404 Not Found
- File not found in serve directory
- Invalid UUID/name

### 413 Payload Too Large
- File exceeds 128MB limit

### 500 Internal Server Error
- Failed to create/write file
- I/O errors
- Unknown server errors

## Configuration

### Constants (defined in main.cpp)

```cpp
constexpr int SERVER_PORT = 8080;
constexpr int MAX_CONNECTIONS = 128;
constexpr int REQUEST_TIMEOUT = 30;
constexpr size_t MAX_FILE_SIZE = 128 * 1024 * 1024;  // 128MB
```

To modify, edit the source and recompile:
```bash
bash a.sh
```

## Security

### Filename Validation
- Filenames are server-generated (UUIDs) - no user input
- Original extensions validated against whitelist
- Path traversal prevention
- No directory separators allowed

### File Size Limits
- Maximum upload size: 128MB
- Prevents DoS attacks

### Request Timeout
- 30-second timeout for receiving requests
- Prevents slowloris attacks

### File Permissions
- Uploaded files: `0600` (read/write for owner only)
- Prevents unauthorized access

### CORS
- Wildcard origin (`Access-Control-Allow-Origin: *`)
- Restrict in production if needed

## Logging

All requests are logged to `./server.log`:

```
[2026-02-19 02:49:53] INFO  | 127.0.0.1:38522 | POST /upload | 200 | Uploaded 2382651 bytes as 18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c.jpg, compressing to 18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c.webp
[2026-02-19 02:50:17] INFO  | 127.0.0.1:47356 | HEAD <uuid>.webp | 200 | Metadata sent from serve directory
[2026-02-19 02:52:50] INFO  | 127.0.0.1:59410 | GET <uuid>.webp | 304 | Cache hit (ETag)
```

Log format:
- Timestamp
- Log level (INFO/WARN/ERROR)
- Client IP:port
- Method and path
- Status code
- Message

## Example Workflow

### Upload and Retrieve an Image

```bash
# 1. Start the server
./a

# 2. Upload an image
curl -X POST "http://localhost:8080/upload" \
    --data-binary @/path/to/photo.jpg \
    -H "Content-Type: image/jpeg"

# Response: {"name":"18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c.webp"}

# 3. Retrieve the compressed WebP
curl "http://localhost:8080/retrieve?name=18957261e0990809642608d971e9ea626cf328ca9f4893799d18e8c5f68319ae7ef833638be93e2c.webp" \
    --output photo.webp

# 4. Display the image
# (Use any WebP-compatible viewer)
```

### JavaScript Example

```javascript
// Upload file
async function uploadImage(file) {
  const response = await fetch('http://localhost:8080/upload', {
    method: 'POST',
    body: file,
    headers: {
      'Content-Type': file.type
    }
  });
  const data = await response.json();
  return data.name; // e.g., "1895726...e93e2c.webp"
}

// Retrieve and display
async function displayImage(name) {
  const response = await fetch(`http://localhost:8080/retrieve?name=${name}`);
  const blob = await response.blob();
  const url = URL.createObjectURL(blob);
  const img = document.createElement('img');
  img.src = url;
  document.body.appendChild(img);
}

// Usage
const files = document.getElementById('fileInput').files;
if (files.length > 0) {
  const name = await uploadImage(files[0]);
  await displayImage(name);
}
```

## Performance

### Memory Management
- RAII (Resource Acquisition Is Initialization) ensures automatic cleanup
- Scoped file descriptors prevent leaks
- Smart container usage (std::string, std::vector)
- No manual malloc/free

### Benchmarks
- Request handling: Sub-millisecond
- Small file upload: ~1-2ms
- Large file upload (10MB): ~100ms
- File retrieval: ~1-5ms (excluding I/O)
- Compression: ~500ms-2s (depends on image size, runs in background)

## Troubleshooting

### Port Already in Use

```bash
# Check what's using port 8080
netstat -tlnp | grep :8080
# or
lsof -i :8080

# Kill the process or change SERVER_PORT in main.cpp
```

### Compression Not Working

```bash
# Verify ImageMagick is installed
which convert

# Test compressor.sh manually
./compressor.sh input.jpg output.webp

# Check server logs
tail -f server.log
```

### File Not Found on Retrieve

```bash
# Check if file exists in serve/
ls serve/

# Check server logs for the filename
grep "Uploaded" server.log

# Verify filename is correct (including .webp extension)
```

## Dependencies

### Runtime
- C++17 standard library
- POSIX system calls (Linux/Unix)

### Build
- g++ (C++17 support)
- make (not required, a.sh handles compilation)

### Optional
- ImageMagick (for compressor.sh)

## Project Structure

```
.
├── main.cpp            # Server entry point, request routing
├── handlers.cpp/.hpp   # HTTP method handlers
├── http_response.cpp/.hpp  # HTTP response helpers
├── utils.cpp/.hpp      # Utilities (UUID, file detection, etc.)
├── logging.cpp/.hpp    # Logging implementation
├── a.sh                # Build script
├── compressor.sh       # WebP compression script
├── serve/              # Public files (created at runtime)
├── save/               # Upload storage (created at runtime)
└── server.log          # Request logs (created at runtime)
```

## License

MIT License - see [LICENSE](LICENSE) for details.

## Author

ASAyman
