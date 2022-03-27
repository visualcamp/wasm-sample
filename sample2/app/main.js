const http = require("http");
const fs = require('fs').promises;

const host = 'localhost';
const port = 8000;

const requestListener = function (req, res) {
    let url = req.url;
    if (url === "/") url = "/index.html";
    
    let contentType = null;
    if (url.endsWith('.html')) {
        contentType = "text/html";    
    } else if (url.endsWith('.js')) {
        contentType = "text/javascript";
    } else if (url.endsWith('.wasm')) {
        contentType = "application/wasm";
    }
    if (contentType) {
        res.setHeader("Content-Type", contentType);
    }
    
    console.log("target : ", __dirname + url);
    fs.readFile(__dirname + url)
        .then(contents => {
            res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
            res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
            res.writeHead(200);        
            res.end(contents);
        })
        .catch(err => {
            console.log("error: ", url);
            console.log("Error", err.stack);
            console.log("Error", err.name);
            console.log("Error", err.message);
            res.writeHead(500);
            return;
        });
};

const server = http.createServer(requestListener);
server.listen(port, host, () => {
    console.log(`Server is running on http://${host}:${port}`);
});
