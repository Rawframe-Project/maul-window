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

// A point of an element, from its top left, in the page.
async function pointOf(tab, selector, x, y) {
    const box = await tab.$eval(selector, element => {
        const rect = element.getBoundingClientRect();
        return [rect.left + element.clientLeft, rect.top + element.clientTop];
    });
    return [box[0] + Number(x), box[1] + Number(y)];
}

// A touch at a point, down, moved and up, through the DevTools protocol.
async function touch(tab, x, y) {
    const session = await tab.createCDPSession();
    const point = (dx, dy) => [{x: x + dx, y: y + dy, id: 1, force: 0.5, radiusX: 2, radiusY: 2}];
    await session.send('Input.dispatchTouchEvent', {type: 'touchStart', touchPoints: point(0, 0)});
    await session.send('Input.dispatchTouchEvent', {type: 'touchMove', touchPoints: point(10, 5)});
    await session.send('Input.dispatchTouchEvent', {type: 'touchEnd', touchPoints: []});
}

// A pen hovering with its barrel button held, down, and up with it let go.
async function pen(tab, x, y) {
    const session = await tab.createCDPSession();
    const send = (type, button, buttons, force) => session.send('Input.dispatchMouseEvent', {
        type, x, y, button, buttons, force, tiltX: 30, tiltY: -15, pointerType: 'pen',
        clickCount: type === 'mouseMoved' ? 0 : 1});
    await send('mouseMoved', 'none', 2, 0);
    await send('mousePressed', 'left', 3, 0.25);
    await send('mouseReleased', 'left', 0, 0);
}

// The commands a test may give.
async function carryOut(tab, command, args) {
    if (command === 'scale') {
        await tab.setViewport({width: 1024, height: 768, deviceScaleFactor: Number(args[0])});
    } else if (command === 'scheme') {
        await tab.emulateMediaFeatures([{name: 'prefers-color-scheme', value: args[0]}]);
    } else if (command === 'key' || command === 'down' || command === 'up') {
        await {key: tab.keyboard.press, down: tab.keyboard.down,
               up: tab.keyboard.up}[command].call(tab.keyboard, args[0]);
    } else if (command === 'move') {
        const [x, y] = await pointOf(tab, args[0], args[1], args[2]);
        await tab.mouse.move(x, y);
    } else if (command === 'press' || command === 'release') {
        await tab.mouse[command === 'press' ? 'down' : 'up']({button: args[0]});
    } else if (command === 'click') {
        const [x, y] = await pointOf(tab, args[0], args[1], args[2]);
        await tab.mouse.click(x, y);
    } else if (command === 'wheel') {
        await tab.mouse.wheel({deltaX: Number(args[0]), deltaY: Number(args[1])});
    } else if (command === 'touch') {
        const [x, y] = await pointOf(tab, args[0], args[1], args[2]);
        await touch(tab, x, y);
    } else if (command === 'compose' || command === 'commit') {
        // An input method's composition, and its result.
        const session = await tab.createCDPSession();
        if (command === 'compose') {
            await session.send('Input.imeSetComposition', {text: args[0],
                selectionStart: Number(args[1]), selectionEnd: Number(args[2])});
        } else {
            await session.send('Input.insertText', {text: args[0]});
        }
    } else if (command === 'pen') {
        const [x, y] = await pointOf(tab, args[0], args[1], args[2]);
        await pen(tab, x, y);
    } else {
        throw new Error('unknown command ' + command);
    }
}

// Commands run one after another, in the order the test gave them.
let queue = Promise.resolve();

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
                    queue = queue.then(() => carryOut(tab, match[1], match[2].split(' ')))
                                 .catch(reject);
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
