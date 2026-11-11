/* global document */
// The split audio fixture: a 2 s, 440 Hz sine WAV built in the page (no binary), looping at a very low volume in an
// <audio>, a Play button and #state with playing and paused.
;(() => {
  const rate = 8000
  const samples = rate * 2
  const view = new DataView(new ArrayBuffer(44 + samples * 2))
  const text = (at, s) => [...s].forEach((c, i) => view.setUint8(at + i, c.charCodeAt(0)))
  text(0, 'RIFF')
  view.setUint32(4, 36 + samples * 2, true)
  text(8, 'WAVEfmt ')
  view.setUint32(16, 16, true)
  view.setUint16(20, 1, true)
  view.setUint16(22, 1, true)
  view.setUint32(24, rate, true)
  view.setUint32(28, rate * 2, true)
  view.setUint16(32, 2, true)
  view.setUint16(34, 16, true)
  text(36, 'data')
  view.setUint32(40, samples * 2, true)
  for (let i = 0; i < samples; i++) view.setInt16(44 + i * 2, Math.round(Math.sin((2 * Math.PI * 440 * i) / rate) * 0.1 * 32767), true)
  const audio = document.querySelector('audio')
  audio.volume = 0.1
  audio.src = URL.createObjectURL(new Blob([view.buffer], { type: 'audio/wav' }))
  const state = document.getElementById('state')
  const show = () => {
    state.textContent = JSON.stringify({ playing: !audio.paused, paused: audio.paused })
  }
  for (const ev of ['play', 'pause']) audio.addEventListener(ev, show)
  document.getElementById('play').addEventListener('click', () => void audio.play())
  show()
})()
