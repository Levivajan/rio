/* global document */
// Shared by the split video fixtures: a live canvas stream (duration Infinity) in a 480x270 <video>, a Play button,
// and #state with playing, paused and pip. The <video> gets the attributes from data-video on <body>.
;(() => {
  const video = document.querySelector('video')
  const canvas = document.createElement('canvas')
  canvas.width = 320
  canvas.height = 180
  const g = canvas.getContext('2d')
  let frame = 0
  setInterval(() => {
    frame += 1
    g.fillStyle = `hsl(${(frame * 4) % 360} 70% 45%)`
    g.fillRect(0, 0, 320, 180)
    g.fillStyle = '#fff'
    g.font = '32px sans-serif'
    g.fillText(String(frame), 20, 100)
  }, 50)
  video.srcObject = canvas.captureStream(20)
  const state = document.getElementById('state')
  const show = () => {
    state.textContent = JSON.stringify({ playing: !video.paused, paused: video.paused, pip: document.pictureInPictureElement === video })
  }
  for (const ev of ['play', 'pause', 'enterpictureinpicture', 'leavepictureinpicture']) video.addEventListener(ev, show)
  document.getElementById('play').addEventListener('click', () => void video.play())
  show()
})()
