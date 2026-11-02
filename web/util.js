// Small helpers shared by the user interface modules.

export const $ = id => document.getElementById(id);
export const nums = text => (text || '').trim().split(/\s+/).filter(Boolean).map(Number);

export const api = {
    async get(path) { return (await fetch(path, { cache: 'no-store' })).json(); },
    async post(path, body) {
        const r = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body });
        return r.json();
    },
};

const timers = {};
// runs fn after the last call with this key has been quiet for `delay` ms
export function soon(key, fn, delay = 120) { clearTimeout(timers[key]); timers[key] = setTimeout(fn, delay); }
