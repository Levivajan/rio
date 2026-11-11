import { describe, expect, it } from 'vitest'
import { looksLikeSource } from './LiveFolderPrompt'

describe('looksLikeSource', () => {
  it('takes feed addresses with or without a scheme, and refuses junk', () => {
    expect(looksLikeSource('feed', 'news.example.com/feed.xml')).toBe(true)
    expect(looksLikeSource('feed', ' https://news.example.com/rss ')).toBe(true)
    for (const junk of ['', 'not a feed', 'localhost', 'javascript:alert(1)', 'file:///etc/hosts', 'https://u:p@a.example/']) {
      expect(looksLikeSource('feed', junk)).toBe(false)
    }
  })

  it('takes owner/repo and GitHub repository URLs for the GitHub kinds', () => {
    expect(looksLikeSource('githubReleases', 'chromium/chromium')).toBe(true)
    expect(looksLikeSource('githubCommits', 'https://github.com/a-b/c.d')).toBe(true)
    expect(looksLikeSource('githubReleases', 'just-one')).toBe(false)
    expect(looksLikeSource('githubReleases', 'a b/c')).toBe(false)
  })
})
