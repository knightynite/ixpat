// Thrum haptics for Photoshop (UXP). Untested sample: install with the UXP
// Developer Tool ("Add plugin" → this folder → Load), paste the token from
// Thrum's Diagnostics tab, and pulses follow Photoshop's own events.
//
// Photoshop does not expose alignment-guide snapping to plugins; Logitech's
// plugin gets that through a private hook. What the public API does give is
// below. Add more entries to EVENTS to taste.
const { action } = require("photoshop");

const THRUM = "http://127.0.0.1:47337/haptic";
let token = localStorage.getItem("thrum.token") || "";

const EVENTS = {
  historyStateChanged: "step",   // every committed edit: a small detent
  select:              "hover",  // layer / tool selection
  set:                 "hover",
  move:                "align",  // a layer moved and committed
  transform:           "align",
  delete:              "collide",
  generateImage:       "success" // Generative Fill finished (Firefly)
};

async function pulse(event, intensity = 3) {
  try {
    await fetch(THRUM, { method: "POST", headers: { "content-type": "application/json" },
                         body: JSON.stringify({ token, event, intensity }) });
  } catch (e) { /* Thrum not running */ }
}

action.addNotificationListener(Object.keys(EVENTS).map(event => ({ event })), (event) => {
  const wave = EVENTS[event];
  if (wave) pulse(wave);
});

// Minimal panel: token field + test button.
document.body.innerHTML = `
  <style>body{font:12px -apple-system,system-ui;padding:10px;color:#ddd}input{width:100%;margin:6px 0}</style>
  <div>Thrum token</div><input id="tok" type="password" value="${token}">
  <button id="test">Test pulse</button> <span id="st"></span>`;
document.getElementById("tok").addEventListener("change", e => { token = e.target.value.trim(); localStorage.setItem("thrum.token", token); });
document.getElementById("test").addEventListener("click", async () => {
  document.getElementById("st").textContent = "…"; await pulse("click", 4); document.getElementById("st").textContent = "sent";
});
