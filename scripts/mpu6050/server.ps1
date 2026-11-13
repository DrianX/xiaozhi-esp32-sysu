param(
    [int]$PreferredPort = 8080
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Split-Path -Parent $MyInvocation.MyCommand.Path)).Path.TrimEnd('\')
$rootPrefix = $root + '\'

$listener = $null
$port = 0
for ($candidate = $PreferredPort; $candidate -lt ($PreferredPort + 25); $candidate++) {
    $candidateListener = New-Object System.Net.HttpListener
    $candidateListener.Prefixes.Add("http://localhost:$candidate/")
    try {
        $candidateListener.Start()
        $listener = $candidateListener
        $port = $candidate
        break
    }
    catch {
        try { $candidateListener.Close() } catch { }
    }
}

if (-not $listener) {
    Write-Host ""
    Write-Host "Could not start a local web server on ports $PreferredPort-$($PreferredPort + 24)." -ForegroundColor Red
    Write-Host "Close any program using those ports and try again." -ForegroundColor Yellow
    Write-Host ""
    exit 1
}

$url = "http://localhost:$port/"
Write-Host ""
Write-Host "  MPU-6050 Telemetry" -ForegroundColor Cyan
Write-Host "  Local server: $url" -ForegroundColor Green
Write-Host "  Keep this window open while using the dashboard." -ForegroundColor DarkGray
Write-Host "  Press Ctrl+C to stop." -ForegroundColor DarkGray
Write-Host ""

$browserCandidates = @(
    (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Google\Chrome\Application\chrome.exe'),
    (Join-Path $env:LOCALAPPDATA 'Google\Chrome\Application\chrome.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe')
)
$browser = $browserCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1

if ($browser) {
    Write-Host "  Opening with: $browser" -ForegroundColor DarkGray
    Start-Process -FilePath $browser -ArgumentList $url
}
else {
    Write-Host "  Chrome/Edge not found. Open this URL manually: $url" -ForegroundColor Yellow
    Start-Process $url
}

$mimeTypes = @{
    '.html' = 'text/html; charset=utf-8'
    '.htm'  = 'text/html; charset=utf-8'
    '.js'   = 'text/javascript; charset=utf-8'
    '.mjs'  = 'text/javascript; charset=utf-8'
    '.css'  = 'text/css; charset=utf-8'
    '.json' = 'application/json; charset=utf-8'
    '.map'  = 'application/json; charset=utf-8'
    '.svg'  = 'image/svg+xml'
    '.png'  = 'image/png'
    '.jpg'  = 'image/jpeg'
    '.jpeg' = 'image/jpeg'
    '.webp' = 'image/webp'
    '.ico'  = 'image/x-icon'
    '.glb'  = 'model/gltf-binary'
    '.gltf' = 'model/gltf+json'
    '.wasm' = 'application/wasm'
    '.step' = 'application/step'
    '.stp'  = 'application/step'
    '.txt'  = 'text/plain; charset=utf-8'
    '.csv'  = 'text/csv; charset=utf-8'
    '.woff2' = 'font/woff2'
}

try {
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        $request = $context.Request
        $response = $context.Response

        try {
            if ($request.HttpMethod -ne 'GET' -and $request.HttpMethod -ne 'HEAD') {
                $response.StatusCode = 405
                $response.ContentType = 'text/plain; charset=utf-8'
                $body = [Text.Encoding]::UTF8.GetBytes('Method Not Allowed')
                $response.ContentLength64 = $body.Length
                if ($request.HttpMethod -ne 'HEAD') { $response.OutputStream.Write($body, 0, $body.Length) }
                continue
            }

            $path = [Uri]::UnescapeDataString($request.Url.AbsolutePath)
            if ([string]::IsNullOrWhiteSpace($path) -or $path -eq '/') {
                $path = '/index.html'
            }

            $relative = $path.TrimStart('/').Replace('/', [IO.Path]::DirectorySeparatorChar)
            $fullPath = [IO.Path]::GetFullPath((Join-Path $root $relative))

            if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                $response.StatusCode = 403
                $response.ContentType = 'text/plain; charset=utf-8'
                $body = [Text.Encoding]::UTF8.GetBytes('Forbidden')
                $response.ContentLength64 = $body.Length
                if ($request.HttpMethod -ne 'HEAD') { $response.OutputStream.Write($body, 0, $body.Length) }
                continue
            }

            if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
                $response.StatusCode = 404
                $response.ContentType = 'text/plain; charset=utf-8'
                $body = [Text.Encoding]::UTF8.GetBytes('Not Found')
                $response.ContentLength64 = $body.Length
                if ($request.HttpMethod -ne 'HEAD') { $response.OutputStream.Write($body, 0, $body.Length) }
                continue
            }

            $extension = [IO.Path]::GetExtension($fullPath).ToLowerInvariant()
            $contentType = if ($mimeTypes.ContainsKey($extension)) { $mimeTypes[$extension] } else { 'application/octet-stream' }
            $fileInfo = Get-Item -LiteralPath $fullPath

            $response.StatusCode = 200
            $response.ContentType = $contentType
            $response.ContentLength64 = $fileInfo.Length

            # Cache immutable assets so the 3D model and vendor libraries do not
            # need to be re-downloaded on every page open. App code stays
            # no-store so edits take effect immediately.
            $cacheControl = 'no-store, no-cache, must-revalidate'
            $isImmutableAsset = $relative.StartsWith('vendor\', [StringComparison]::OrdinalIgnoreCase) -or
                                $relative.StartsWith('assets\', [StringComparison]::OrdinalIgnoreCase)
            if ($isImmutableAsset) {
                $cacheControl = 'public, max-age=604800, immutable'
                $response.Headers.Add('Last-Modified', $fileInfo.LastWriteTimeUtc.ToString('R'))
            }
            $response.Headers.Add('Cache-Control', $cacheControl)
            $response.Headers.Add('X-Content-Type-Options', 'nosniff')
            $response.Headers.Add('Permissions-Policy', 'serial=(self)')

            if ($request.HttpMethod -ne 'HEAD') {
                $stream = [IO.File]::OpenRead($fullPath)
                try {
                    $stream.CopyTo($response.OutputStream, 65536)
                }
                finally {
                    $stream.Dispose()
                }
            }
        }
        catch {
            try {
                $response.StatusCode = 500
                $response.ContentType = 'text/plain; charset=utf-8'
                $body = [Text.Encoding]::UTF8.GetBytes('Internal Server Error')
                $response.ContentLength64 = $body.Length
                if ($request.HttpMethod -ne 'HEAD') { $response.OutputStream.Write($body, 0, $body.Length) }
            }
            catch { }
            Write-Host ("  [500] " + $request.Url.PathAndQuery + " - " + $_.Exception.Message) -ForegroundColor DarkRed
        }
        finally {
            try { $response.Close() } catch { }
        }
    }
}
finally {
    try { $listener.Stop(); $listener.Close() } catch { }
}
