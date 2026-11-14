// HttpLocalServer · trusted application
import { fs, crypto } from 'hls';

export function onRequest(request) {
    if (request.url === '/api/app/status') {
        return {
            body: JSON.stringify({ status: 'ready', mode: 'local' }),
            contentType: 'application/json'
        };
    }
    return {}; // static content lives under public/
}
