// Product page behaviour: two scroll-scrubbed frame sequences, the layer tabs,
// the LED effect chips and the edition switch.  No framework, no build step.
"use strict";

const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;
const LAYER_GLOW = { media: "#8b5cf6", edit: "#14b8a6", fkeys: "#f59e0b", host: "#38bdf8", fn: "#ef4444" };

// ------------------------------------------------------------------ scroll-scrubbed sequences
// A tall section holds a sticky stage; how far the section has scrolled past picks the frame.
function frameUrl(name, i) {
  return `images/site/${name}_${String(i).padStart(2, "0")}.webp`;
}

function setUpScrub(section) {
  const name = section.dataset.scrub;
  const count = Number(section.dataset.frames);
  const canvas = section.querySelector("canvas");
  const ctx = canvas.getContext("2d");
  const callouts = [...section.querySelectorAll(".callouts li")];
  const frames = new Array(count);
  let shown = -1;

  function draw(i) {
    const img = frames[i];
    if (!img || !img.complete || !img.naturalWidth) return false;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
    shown = i;
    return true;
  }

  function load(i) {
    if (frames[i]) return frames[i];
    const img = new Image();
    img.decoding = "async";
    img.src = frameUrl(name, i);
    img.onload = () => { if (i === wanted() || shown < 0) draw(i); };
    frames[i] = img;
    return img;
  }

  function progress() {
    const r = section.getBoundingClientRect();
    const travel = r.height - innerHeight;
    return travel > 0 ? Math.min(1, Math.max(0, -r.top / travel)) : 1;
  }

  function wanted() {
    return Math.round(progress() * (count - 1));
  }

  if (reducedMotion) {
    load(count - 1);
    callouts.forEach((li) => li.classList.add("on"));
    return;
  }

  // load every frame once the section is near; the first frame right away
  load(0);
  new IntersectionObserver((entries, obs) => {
    if (entries.some((e) => e.isIntersecting)) {
      for (let i = 0; i < count; i++) load(i);
      obs.disconnect();
    }
  }, { rootMargin: "100% 0px" }).observe(section);

  let ticking = false;
  function update() {
    ticking = false;
    const p = progress();
    const i = Math.round(p * (count - 1));
    if (i !== shown) draw(i);
    for (const li of callouts) li.classList.toggle("on", p >= Number(li.dataset.at));
  }
  addEventListener("scroll", () => {
    if (!ticking) { ticking = true; requestAnimationFrame(update); }
  }, { passive: true });
  addEventListener("resize", update);
  update();
}

document.querySelectorAll("[data-scrub]").forEach(setUpScrub);

// ------------------------------------------------------------------ layer tabs (WAI-ARIA tabs pattern)
const SCREENS = {
  media: ["rotary_ui_02_media_volume", "Media layer: mute, previous, play, next and volume keys, with the knob set to volume.",
          "Media. Play, skip, mute and brightness, with the knob on volume."],
  edit: ["rotary_ui_03_edit_history", "Edit layer: undo, cut, find, redo, save, copy, paste, home, end, enter and backspace, with the knob set to history.",
         "Edit. Cut, copy, paste, find and save, with undo and redo on the knob. Caps lock lights up on the bar."],
  fkeys: ["joystick_ui_05_fkeys_scroll", "F13 to F24 layer: twelve keys no app uses, with the stick set to scroll.",
          "F13 to F24. Eleven keys nothing else claims, ready for OBS scenes or your own global shortcuts."],
  host: ["rotary_ui_06_host_dashboard", "Host layer: CPU, per-core load, memory, swap, disk and network from the connected PC, with the PC's name and clock.",
         "Host. Live CPU, cores, memory, disk and network from your PC, your own status lines on the bar, and keys that run your commands."],
  fn: ["joystick_ui_04_fn_held", "FN layer: bootloader, stick mode, calibration, display brightness, screen, layer jumps and LED effect keys.",
       "FN. Hold the front-left key for brightness, LED effects, layer jumps, stick calibration and the bootloader."],
};

const tabs = [...document.querySelectorAll('[role="tab"]')];
const panel = document.getElementById("p-screen");
const screenImg = panel.querySelector(".screen-img");
const screenCap = panel.querySelector(".screen-cap");

function selectTab(tab, focus) {
  for (const t of tabs) {
    const on = t === tab;
    t.setAttribute("aria-selected", String(on));
    t.tabIndex = on ? 0 : -1;
  }
  const layer = tab.dataset.layer;
  const [file, alt, caption] = SCREENS[layer];
  screenImg.src = `images/site/${file}.webp`;
  screenImg.alt = alt;
  screenCap.textContent = caption;
  panel.setAttribute("aria-labelledby", tab.id);
  setGlow(LAYER_GLOW[layer]);
  if (focus) tab.focus();
}

tabs.forEach((tab, i) => {
  tab.addEventListener("click", () => selectTab(tab, false));
  tab.addEventListener("keydown", (e) => {
    const step = { ArrowRight: 1, ArrowLeft: -1 }[e.key];
    if (step) selectTab(tabs[(i + step + tabs.length) % tabs.length], true);
    else if (e.key === "Home") selectTab(tabs[0], true);
    else if (e.key === "End") selectTab(tabs[tabs.length - 1], true);
    else return;
    e.preventDefault();
  });
});

// ------------------------------------------------------------------ toggle groups (aria-pressed chips)
function pressGroup(button) {
  for (const b of button.parentElement.querySelectorAll("button")) b.setAttribute("aria-pressed", String(b === button));
}

const lightShot = document.querySelector(".light-shot");
function setGlow(colour) {
  lightShot.style.setProperty("--glow", colour);
}

document.querySelectorAll("[data-effect]").forEach((b) => {
  if (b.tagName !== "BUTTON") return;
  b.addEventListener("click", () => {
    pressGroup(b);
    lightShot.dataset.effect = b.dataset.effect;
  });
});

const centreImg = document.querySelector(".centre-shot img");
document.querySelectorAll("[data-edition]").forEach((b) => {
  b.addEventListener("click", () => {
    pressGroup(b);
    const ed = b.dataset.edition;
    for (const c of document.querySelectorAll(".centre-copy")) c.hidden = c.dataset.show !== ed;
    const next = `images/site/${ed}_top.webp`;
    if (centreImg.getAttribute("src") === next) return;
    centreImg.classList.add("swap");
    setTimeout(() => {
      centreImg.src = next;
      centreImg.alt = `The ${ed} edition from above.`;
      centreImg.classList.remove("swap");
    }, reducedMotion ? 0 : 250);
  });
});
