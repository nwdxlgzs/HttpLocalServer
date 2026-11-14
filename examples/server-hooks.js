// Trusted host script: --script examples/server-hooks.js
// QuickJS-NG has no filesystem/network/process/module bindings.
function onRequest(request) {
    if (request.url === '/health') {
        return {contentType:'application/json; charset=utf-8',body:JSON.stringify({ok:true,mode:request.mode}),headers:{'Cache-Control':'no-store'}};
    }
    if (request.url === '/docs') return {rewrite:'/index.html'};
    // Controlled writes require the normal login, write scope and X-HLS-Token.
    // POST /save-note with a <=64 KiB body publishes a new file without overwrite.
    if (request.url === '/save-note') return {write:{path:'/note.txt',text:request.body}};
    return {}; // Continue normal hosting / preview routing.
}
function onFileOperation(event) {
    // Invoked for native/replica uploads, read, copy, delete, mkdir, rename and restore.
    // This policy can restrict access; it never grants privileges or unfreezes a file.
    if (event.operation === 'upload' && /\.exe$/i.test(event.path)) return {allow:false,message:'This host does not accept executable uploads'};
    return {allow:true};
}
