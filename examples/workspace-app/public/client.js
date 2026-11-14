// SPDX-License-Identifier: Apache-2.0
(function(){
    'use strict';
    document.getElementById('add').onsubmit=function(event){
        event.preventDefault();var request=new XMLHttpRequest(),out=document.getElementById('result');
        request.open('POST','/api/app/add',true);request.setRequestHeader('Content-Type','application/json');request.setRequestHeader('X-HLS-App','1');
        request.onreadystatechange=function(){if(request.readyState===4)out.textContent=request.responseText;};
        request.send(JSON.stringify({a:Number(document.getElementById('a').value),b:Number(document.getElementById('b').value)}));
    };
}());
