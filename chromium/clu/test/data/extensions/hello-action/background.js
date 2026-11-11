/* global chrome */
// The service worker opens a tab when asked, the way a real extension does from its background.
chrome.runtime.onMessage.addListener((message, _sender, reply) => {
  if (message?.type !== 'open-tab') return false
  chrome.tabs.create({ url: message.url }).then(
    (tab) => reply({ id: tab.id }),
    (err) => reply({ error: String(err) })
  )
  return true
})
