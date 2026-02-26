export interface SocialEntry {
  type: 'github' | 'twitter' | 'email'
  icon: string
  link: string
}

export interface Creator {
  avatar: string
  name: string
  username?: string
  title?: string
  org?: string
  desc?: string
  links?: SocialEntry[]
  nameAliases?: string[]
  emailAliases?: string[]
}

const getAvatarUrl = (name: string) => `https://github.com/${name}.png`

export const creators: Creator[] = [
  {
    name: 'Kong',
    avatar: 'https://avatars.githubusercontent.com/u/187236942?v=4',
    username: 'ZhiKong0',
    title: '作者',
    desc: '嵌入式开发者，记录学习成长历程',
    links: [
      { type: 'github', icon: 'github', link: 'https://github.com/ZhiKong0' },
    ],
    nameAliases: ['Kong'],
    emailAliases: [],
  },
].map<Creator>((c) => {
  c.avatar = c.avatar || getAvatarUrl(c.username)
  return c as Creator
})

export const creatorNames = creators.map(c => c.name)
export const creatorUsernames = creators.map(c => c.username || '')
