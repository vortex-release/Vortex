param([Parameter(Mandatory)][string]$Server,[Parameter(Mandatory)][string]$TestDirectory)
$ErrorActionPreference='Stop'
$directory=Join-Path $TestDirectory ('http-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $directory 'releases.win.json'),'{"Assets":[]}')
[IO.File]::WriteAllText((Join-Path $directory 'private.cpp'),'private source')
[IO.File]::WriteAllText((Join-Path $directory 'index.html'),'<h1>Vortex</h1>')
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start()
$port=$listener.LocalEndpoint.Port;$listener.Stop()
$process=Start-Process -FilePath $Server -ArgumentList @(('"' + $directory + '"'),$port) -WindowStyle Hidden -PassThru
function Request([string]$Line) {
    $client=[Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1',$port);$stream=$client.GetStream();$stream.ReadTimeout=3000
        $crlf=[string][char]13+[char]10
        $bytes=[Text.Encoding]::ASCII.GetBytes($Line + $crlf + 'Host: localhost' + $crlf + 'Connection: close' + $crlf + $crlf)
        $stream.Write($bytes,0,$bytes.Length);$reader=[IO.StreamReader]::new($stream);return $reader.ReadToEnd()
    } finally {$client.Dispose()}
}
try {
    Start-Sleep -Milliseconds 300
    if ($process.HasExited) {throw 'Server exited early.'}
    $response=Request 'GET /releases.win.json HTTP/1.1'
    if($response -notmatch '200 OK' -or $response -notmatch '\{"Assets":\[\]\}') {throw 'Feed retrieval failed.'}
    $head=Request 'HEAD /releases.win.json HTTP/1.1'
    if($head -notmatch '200 OK' -or $head -match '\{"Assets"') {throw 'HEAD must return headers without a body.'}
    foreach($path in @('/../release/config.json','/%2e%2e/config.json','/private.cpp','/C:/Windows/win.ini','/nested/index.html')){
        if((Request "GET $path HTTP/1.1") -notmatch '404 Not found'){throw "Unexpected access: $path"}
    }
    if((Request 'POST /releases.win.json HTTP/1.1') -notmatch '405 Method not allowed'){throw 'Writes were accepted.'}
    if((Request 'GET / HTTP/1.1') -notmatch '<h1>Vortex</h1>'){throw 'Download page missing.'}
    Write-Output 'PASS: feed, HEAD, download page, restricted paths, source filtering, and read-only methods.'
} finally {
    if(-not $process.HasExited){$process.Kill();$process.WaitForExit()}
}
