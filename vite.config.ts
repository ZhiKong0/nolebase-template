import { defineConfig } from 'vite'
import Components from 'unplugin-vue-components/vite'
import UnoCSS from 'unocss/vite'
import Inspect from 'vite-plugin-inspect'

import { GitChangelog, GitChangelogMarkdownSection } from '@nolebase/vitepress-plugin-git-changelog/vite'
import { PageProperties, PagePropertiesMarkdownSection } from '@nolebase/vitepress-plugin-page-properties/vite'
import { ThumbnailHashImages } from '@nolebase/vitepress-plugin-thumbnail-hash/vite'

import { githubRepoLink } from './metadata'

export default defineConfig(async () => {
  return {
    assetsInclude: ['**/*.mov'],
    optimizeDeps: {
      exclude: ['vitepress'],
    },
    plugins: [
      Inspect(),

      // ✅ 仍然保留文件历史（时间轴），但不再用它展示人
      GitChangelog({
        repoURL: () => githubRepoLink,
      }),

      // ✅ 关键：彻底关闭“贡献者卡片”
      GitChangelogMarkdownSection({
        getChangelogTitle: () => '文件历史',
        excludes: ['toc.md', 'index.md'],
        sections: {
          disableContributors: true, // 🔥 核心：不再显示贡献者
        },
      }),

      // ✅ 用 PageProperties 来承载“作者信息”
      PageProperties(),
      PagePropertiesMarkdownSection({
        excludes: ['toc.md', 'index.md'],
      }),

      ThumbnailHashImages(),

      Components({
        include: [/\.vue$/, /\.md$/],
        dirs: '.vitepress/theme/components',
        dts: '.vitepress/components.d.ts',
      }),

      UnoCSS(),
    ],
    ssr: {
      noExternal: [
        '@nolebase/vitepress-plugin-enhanced-readabilities',
        '@nolebase/vitepress-plugin-highlight-targeted-heading',
        '@nolebase/vitepress-plugin-inline-link-preview',
      ],
    },
  }
})
