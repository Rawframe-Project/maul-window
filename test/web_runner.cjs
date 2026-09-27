// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs a web test in headless Chrome: serves the test's directory, opens
// a page with a canvas of its own (#page-canvas) that loads the test,
// and carries out the commands the test prints ("mwin-test: <command>").
// The test ends by printing "mwin-test: exit <status>". Puppeteer comes
// from MWIN_NODE_MODULES; without it the test is skipped (status 77).
//
// usage: node web_runner.cjs <test.js>

const http = require('http');
const fs = require('fs');
const path = require('path');

const modules = process.env.MWIN_NODE_MODULES;
let puppeteer = null;
try {
    puppeteer = require(path.join(modules || '', 'puppeteer'));
} catch (error) {
    console.log('puppeteer not found through MWIN_NODE_MODULES: skipped');
    process.exit(77);
}

const script = path.resolve(process.argv[2]);
const root = path.dirname(script);
const page = `<!doctype html><html><head><meta charset="utf-8"></head><body>
<canvas id="page-canvas" style="width:200px;height:100px"></canvas>
<script src="${path.basename(script)}"></script></body></html>`;
const types = {'.js': 'text/javascript', '.wasm': 'application/wasm'};

const server = http.createServer((request, response) => {
    if (request.url === '/') {
        response.writeHead(200, {'Content-Type': 'text/html'});
        response.end(page);
        return;
    }
    const file = path.join(root, path.normalize(decodeURIComponent(request.url)));
    if (!file.startsWith(root) || !fs.existsSync(file)) {
        response.writeHead(404);
        response.end();
        return;
    }
    response.writeHead(200, {'Content-Type': types[path.extname(file)] || 'application/octet-stream'});
    fs.createReadStream(file).pipe(response);
});

// The commands a test may give.
async function carryOut(tab, command, args) {
    if (command === 'scale') {
        await tab.setViewport({width: 1024, height: 768, deviceScaleFactor: Number(args[0])});
    } else if (command === 'scheme') {
        await tab.emulateMediaFeatures([{name: 'prefers-color-scheme', value: args[0]}]);
    } else {
        throw new Error('unknown command ' + command);
    }
}

async function main() {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const browser = await puppeteer.launch({args: ['--no-sandbox']});
    let status = 1;
    try {
        const tab = await browser.newPage();
        await tab.setViewport({width: 1024, height: 768, deviceScaleFactor: 2});
        await tab.emulateMediaFeatures([{name: 'prefers-reduced-motion', value: 'reduce'}]);
        const done = new Promise((resolve, reject) => {
            tab.on('pageerror', error => reject(error));
            tab.on('console', message => {
                const text = message.text();
                console.log(text);
                const match = /^mwin-test: (\w+) ?(.*)$/.exec(text);
                if (!match) {
                    return;
                }
                if (match[1] === 'exit') {
                    resolve(Number(match[2]));
                } else {
                    carryOut(tab, match[1], match[2].split(' ')).catch(reject);
                }
            });
        });
        await tab.goto(`http://127.0.0.1:${server.address().port}/`);
        const timeout = new Promise((_, reject) =>
            setTimeout(() => reject(new Error('timed out')), 20000));
        status = await Promise.race([done, timeout]);
    } catch (error) {
        console.log('runner: ' + error.message);
    } finally {
        await browser.close();
        server.close();
    }
    process.exit(status);
}

main();
