import{_ as l,B as a,o as e,c as h,j as n,a as d,E as t,az as r}from"./chunks/framework.BARMEsmO.js";const b=JSON.parse('{"title":"CT107D 总线工作原理详解","description":"","frontmatter":{},"headers":[],"relativePath":"笔记/MCU_Learning/C51单片机学习-蓝桥杯单片机备赛/01 基础学习/00 总线工作原理/总线工作原理.md","filePath":"笔记/MCU_Learning/C51单片机学习-蓝桥杯单片机备赛/01 基础学习/00 总线工作原理/总线工作原理.md"}'),k={name:"笔记/MCU_Learning/C51单片机学习-蓝桥杯单片机备赛/01 基础学习/00 总线工作原理/总线工作原理.md"};function o(c,s,g,y,E,B){const i=a("NolebasePageProperties"),p=a("NolebaseGitChangelog");return e(),h("div",null,[s[0]||(s[0]=n("h1",{id:"ct107d-总线工作原理详解",tabindex:"-1"},[d("CT107D 总线工作原理详解 "),n("a",{class:"header-anchor",href:"#ct107d-总线工作原理详解","aria-label":'Permalink to "CT107D 总线工作原理详解"'},"​")],-1)),t(i),s[1]||(s[1]=r(`<h2 id="一、核心芯片概述" tabindex="-1">一、核心芯片概述 <a class="header-anchor" href="#一、核心芯片概述" aria-label="Permalink to &quot;一、核心芯片概述&quot;">​</a></h2><h3 id="_1-1-74hc138-3-8线译码器" tabindex="-1">1.1 74HC138（3-8线译码器） <a class="header-anchor" href="#_1-1-74hc138-3-8线译码器" aria-label="Permalink to &quot;1.1 74HC138（3-8线译码器）&quot;">​</a></h3><table tabindex="0"><thead><tr><th>特性</th><th>说明</th></tr></thead><tbody><tr><td><strong>核心功能</strong></td><td><strong>选择</strong>哪个外设（开关/阀门）</td></tr><tr><td><strong>比喻</strong></td><td>像<strong>分岔路口的阀门</strong>，决定水流向哪条管道</td></tr><tr><td><strong>输入</strong></td><td>3位地址码 (CBA = P2.6 P2.5 P2.4)</td></tr><tr><td><strong>输出</strong></td><td>8路选通信号 (Y0-Y7)，低电平有效</td></tr><tr><td><strong>使能条件</strong></td><td>P2.7=0（WR低电平有效），G1=VCC，G2B=GND</td></tr><tr><td><strong>数量</strong></td><td>1片 (U17)</td></tr></tbody></table><p><strong>一句话描述：74HC138是&quot;地址翻译官&quot;——把P2口的二进制地址翻译成&quot;打开哪扇门&quot;</strong></p><h3 id="_1-2-74hc573-8位锁存器" tabindex="-1">1.2 74HC573（8位锁存器） <a class="header-anchor" href="#_1-2-74hc573-8位锁存器" aria-label="Permalink to &quot;1.2 74HC573（8位锁存器）&quot;">​</a></h3><table tabindex="0"><thead><tr><th>特性</th><th>说明</th></tr></thead><tbody><tr><td><strong>核心功能</strong></td><td><strong>保持</strong>外设状态（记忆/锁存）</td></tr><tr><td><strong>比喻</strong></td><td>像<strong>水桶</strong>，装水后保持水量不变；也像<strong>单向门</strong>，关上门后外面变化不影响里面</td></tr><tr><td><strong>输入</strong></td><td>8位数据 (D0-D7 = P0.0-P0.7)</td></tr><tr><td><strong>输出</strong></td><td>8位保持数据 (Q0-Q7)</td></tr><tr><td><strong>控制端</strong></td><td>LE (Latch Enable)：高电平=直通，低电平=锁存</td></tr><tr><td><strong>数量</strong></td><td>4片 (U1, U2, U3, U6)</td></tr></tbody></table><p><strong>一句话描述：74HC573是&quot;记忆保持器&quot;——记住P0口的数据，即使P0口后续变化也不影响外设</strong></p><hr><h2 id="二、系统架构总览" tabindex="-1">二、系统架构总览 <a class="header-anchor" href="#二、系统架构总览" aria-label="Permalink to &quot;二、系统架构总览&quot;">​</a></h2><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light one-dark-pro vp-code" tabindex="0"><code><span class="line"><span>┌─────────────────────────────────────────────────────────────────┐</span></span>
<span class="line"><span>│                    STC15F2K60S2 单片机                          │</span></span>
<span class="line"><span>│  ┌─────────────────┐        ┌─────────────────┐              │</span></span>
<span class="line"><span>│  │   P2口 (地址总线)│        │   P0口 (数据总线)│              │</span></span>
<span class="line"><span>│  │  P2.7=使能(WR)   │        │  P0.0-P0.7      │              │</span></span>
<span class="line"><span>│  │  P2.6~P2.4=CBA   │        │  连接到所有外设  │              │</span></span>
<span class="line"><span>│  └────────┬─────────┘        └────────┬─────────┘              │</span></span>
<span class="line"><span>│           │                          │                        │</span></span>
<span class="line"><span>│           ▼                          ▼                        │</span></span>
<span class="line"><span>│  ┌─────────────────┐                │                        │</span></span>
<span class="line"><span>│  │   74HC138        │                │                        │</span></span>
<span class="line"><span>│  │   (U17)          │                │                        │</span></span>
<span class="line"><span>│  │   3-8译码器      │                │                        │</span></span>
<span class="line"><span>│  │   &quot;选择器&quot;       │                │                        │</span></span>
<span class="line"><span>│  │                  │                │                        │</span></span>
<span class="line"><span>│  │  决定选通哪个    ├────────────────┘                        │</span></span>
<span class="line"><span>│  │  573的LE端       │                                       │</span></span>
<span class="line"><span>│  └────────┬─────────┘                                       │</span></span>
<span class="line"><span>│           │                                                 │</span></span>
<span class="line"><span>│     ┌─────┴─────────────────┬─────────────────┐            │</span></span>
<span class="line"><span>│     │                       │                 │            │</span></span>
<span class="line"><span>│     ▼                       ▼                 ▼            │</span></span>
<span class="line"><span>│  ┌─────────┐           ┌─────────┐       ┌─────────┐      │</span></span>
<span class="line"><span>│  │ Y4输出  │           │ Y5输出  │       │ Y6输出  │      │</span></span>
<span class="line"><span>│  │(选中U3) │           │(选中U6) │       │(选中U2) │      │</span></span>
<span class="line"><span>│  └────┬────┘           └────┬────┘       └────┬────┘      │</span></span>
<span class="line"><span>│       │                     │                 │            │</span></span>
<span class="line"><span>│       ▼                     ▼                 ▼            │</span></span>
<span class="line"><span>│  ┌─────────┐           ┌─────────┐       ┌─────────┐      │</span></span>
<span class="line"><span>│  │74HC573  │           │74HC573  │       │74HC573  │      │</span></span>
<span class="line"><span>│  │ (U3)    │           │ (U6)    │       │ (U2)    │      │</span></span>
<span class="line"><span>│  │ LED     │           │ ULN     │       │ 位选    │      │</span></span>
<span class="line"><span>│  │ 锁存器  │           │ 锁存器  │       │ 锁存器  │      │</span></span>
<span class="line"><span>│  │         │           │         │       │         │      │</span></span>
<span class="line"><span>│  │ LE◄─────┘           │ LE◄─────┘       │ LE◄─────┘      │</span></span>
<span class="line"><span>│  │ D0-D7◄──────────────┼─────────────┼──────► 都来自P0口！  │</span></span>
<span class="line"><span>│  │                     │             │                     │</span></span>
<span class="line"><span>│  ▼                     ▼             ▼                     │</span></span>
<span class="line"><span>│ 8个LED              蜂鸣器/        数码管                  │</span></span>
<span class="line"><span>│                     继电器         位选                    │</span></span>
<span class="line"><span>└─────────────────────────────────────────────────────────────────┘</span></span></code></pre></div><hr><h2 id="三、74hc138-译码器详解" tabindex="-1">三、74HC138 译码器详解 <a class="header-anchor" href="#三、74hc138-译码器详解" aria-label="Permalink to &quot;三、74HC138 译码器详解&quot;">​</a></h2><h3 id="_3-1-真值表" tabindex="-1">3.1 真值表 <a class="header-anchor" href="#_3-1-真值表" aria-label="Permalink to &quot;3.1 真值表&quot;">​</a></h3><table tabindex="0"><thead><tr><th>P2值</th><th>二进制</th><th>CBA</th><th>Y输出</th><th>选中的外设</th><th>用途</th></tr></thead><tbody><tr><td>0x80</td><td>1000 0000</td><td>100</td><td><strong>Y4</strong></td><td>LED锁存器 (U3)</td><td>控制8个LED</td></tr><tr><td>0xA0</td><td>1010 0000</td><td>101</td><td><strong>Y5</strong></td><td>ULN锁存器 (U6)</td><td>控制蜂鸣器/继电器</td></tr><tr><td>0xC0</td><td>1100 0000</td><td>110</td><td><strong>Y6</strong></td><td>数码管位选锁存器 (U2)</td><td>选通数码管位</td></tr><tr><td>0xE0</td><td>1110 0000</td><td>111</td><td><strong>Y7</strong></td><td>数码管段选锁存器 (U1)</td><td>控制数码管段</td></tr><tr><td>0x00</td><td>0000 0000</td><td>000</td><td>无</td><td>关闭所有锁存器</td><td><strong>必须的操作！</strong></td></tr></tbody></table><h3 id="_3-2-引脚连接" tabindex="-1">3.2 引脚连接 <a class="header-anchor" href="#_3-2-引脚连接" aria-label="Permalink to &quot;3.2 引脚连接&quot;">​</a></h3><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light one-dark-pro vp-code" tabindex="0"><code><span class="line"><span>┌─────────────────────────────────────────┐</span></span>
<span class="line"><span>│           74HC138 (U17)                 │</span></span>
<span class="line"><span>│           3-8线译码器                   │</span></span>
<span class="line"><span>├─────────────────────────────────────────┤</span></span>
<span class="line"><span>│                                         │</span></span>
<span class="line"><span>│  使能输入:                               │</span></span>
<span class="line"><span>│    G1   = VCC  (始终高电平，常使能)      │</span></span>
<span class="line"><span>│    G2A  = P2.7 (WR，低电平有效)         │</span></span>
<span class="line"><span>│    G2B  = GND  (始终低电平，常使能)      │</span></span>
<span class="line"><span>│                                         │</span></span>
<span class="line"><span>│  地址输入 (CBA):                        │</span></span>
<span class="line"><span>│    C = P2.6                             │</span></span>
<span class="line"><span>│    B = P2.5                             │</span></span>
<span class="line"><span>│    A = P2.4                             │</span></span>
<span class="line"><span>│                                         │</span></span>
<span class="line"><span>│  输出 (低电平有效):                     │</span></span>
<span class="line"><span>│    Y0-Y3 = 未使用                        │</span></span>
<span class="line"><span>│    Y4    → 连接到 U3 (LED锁存器) LE端    │</span></span>
<span class="line"><span>│    Y5    → 连接到 U6 (ULN锁存器) LE端    │</span></span>
<span class="line"><span>│    Y6    → 连接到 U2 (位选锁存器) LE端   │</span></span>
<span class="line"><span>│    Y7    → 连接到 U1 (段选锁存器) LE端   │</span></span>
<span class="line"><span>│                                         │</span></span>
<span class="line"><span>└─────────────────────────────────────────┘</span></span></code></pre></div><hr><h2 id="四、74hc573-锁存器详解" tabindex="-1">四、74HC573 锁存器详解 <a class="header-anchor" href="#四、74hc573-锁存器详解" aria-label="Permalink to &quot;四、74HC573 锁存器详解&quot;">​</a></h2><h3 id="_4-1-锁存器工作原理" tabindex="-1">4.1 锁存器工作原理 <a class="header-anchor" href="#_4-1-锁存器工作原理" aria-label="Permalink to &quot;4.1 锁存器工作原理&quot;">​</a></h3><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light one-dark-pro vp-code" tabindex="0"><code><span class="line"><span>┌─────────────────────────────────────────────────────────────┐</span></span>
<span class="line"><span>│                   74HC573 内部工作原理                       │</span></span>
<span class="line"><span>├─────────────────────────────────────────────────────────────┤</span></span>
<span class="line"><span>│                                                             │</span></span>
<span class="line"><span>│   数据输入端         控制端           数据输出端           │</span></span>
<span class="line"><span>│   ┌───────┐                         ┌───────┐              │</span></span>
<span class="line"><span>│   │ D0-D7 │◄─── P0口 (数据总线)      │ Q0-Q7 │───► 外设     │</span></span>
<span class="line"><span>│   └───────┘                         └───────┘              │</span></span>
<span class="line"><span>│       │                                 │                   │</span></span>
<span class="line"><span>│       │         ┌───────────┐         │                   │</span></span>
<span class="line"><span>│       └────────►│   锁存    │─────────┘                   │</span></span>
<span class="line"><span>│                 │   电路    │                             │</span></span>
<span class="line"><span>│                 └─────┬─────┘                             │</span></span>
<span class="line"><span>│                       │                                     │</span></span>
<span class="line"><span>│           LE ◄────────┘ (来自138译码器的Y输出)             │</span></span>
<span class="line"><span>│                                                             │</span></span>
<span class="line"><span>│   OE = GND (始终使能输出)                                   │</span></span>
<span class="line"><span>│                                                             │</span></span>
<span class="line"><span>│   工作状态:                                                  │</span></span>
<span class="line"><span>│   • LE = 高电平: 直通模式 (Q跟随D变化，容易&quot;污染&quot;)         │</span></span>
<span class="line"><span>│   • LE = 低电平: 锁存模式 (Q保持最后状态，不受D变化影响)    │</span></span>
<span class="line"><span>│                                                             │</span></span>
<span class="line"><span>└─────────────────────────────────────────────────────────────┘</span></span></code></pre></div><h3 id="_4-2-ct107d上的4个锁存器" tabindex="-1">4.2 CT107D上的4个锁存器 <a class="header-anchor" href="#_4-2-ct107d上的4个锁存器" aria-label="Permalink to &quot;4.2 CT107D上的4个锁存器&quot;">​</a></h3><table tabindex="0"><thead><tr><th>锁存器</th><th>地址</th><th>Y输出</th><th>控制对象</th><th>有效电平</th></tr></thead><tbody><tr><td>U3</td><td>0x80</td><td>Y4</td><td>8个LED (L1-L8)</td><td><strong>低电平亮</strong></td></tr><tr><td>U6</td><td>0xA0</td><td>Y5</td><td>蜂鸣器(P0.6)、继电器(P0.4)</td><td>高电平驱动</td></tr><tr><td>U2</td><td>0xC0</td><td>Y6</td><td>数码管位选 (COM1-COM8)</td><td><strong>高电平选通</strong></td></tr><tr><td>U1</td><td>0xE0</td><td>Y7</td><td>数码管段选 (a-g, dp)</td><td><strong>低电平亮</strong></td></tr></tbody></table><h2 id="_74hc138-和-74hc573-的关系与区别" tabindex="-1">74HC138 和 74HC573 的关系与区别 <a class="header-anchor" href="#_74hc138-和-74hc573-的关系与区别" aria-label="Permalink to &quot;74HC138 和 74HC573 的关系与区别&quot;">​</a></h2><h3 id="一、芯片功能对比" tabindex="-1">一、芯片功能对比 <a class="header-anchor" href="#一、芯片功能对比" aria-label="Permalink to &quot;一、芯片功能对比&quot;">​</a></h3><table tabindex="0"><thead><tr><th>特性</th><th><strong>74HC138</strong> (3-8译码器)</th><th><strong>74HC573</strong> (8位锁存器)</th></tr></thead><tbody><tr><td><strong>核心功能</strong></td><td><strong>选择</strong>哪个外设</td><td><strong>保持</strong>外设状态</td></tr><tr><td><strong>比喻</strong></td><td>像<strong>开关/阀门</strong> ，决定水流向哪条管道</td><td>像<strong>水桶</strong> ，装水后保持水量不变</td></tr><tr><td><strong>输入</strong></td><td>3位地址码 (CBA)</td><td>8位数据 (D0-D7)</td></tr><tr><td><strong>输出</strong></td><td>8路选通信号 (Y0-Y7)</td><td>8位保持数据 (Q0-Q7)</td></tr><tr><td><strong>CT107D上的数量</strong></td><td>1片 (U17)</td><td>4片 (U1, U2, U3, U6)</td></tr><tr><td><strong>连接到</strong></td><td>P2口高4位</td><td>P0口 (数据总线)</td></tr></tbody></table><h3 id="二、它们在系统中的协作关系" tabindex="-1">二、它们在系统中的协作关系 <a class="header-anchor" href="#二、它们在系统中的协作关系" aria-label="Permalink to &quot;二、它们在系统中的协作关系&quot;">​</a></h3><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light one-dark-pro vp-code" tabindex="0"><code><span class="line"><span>┌────────────────────────────────────────────────────────────┐</span></span>
<span class="line"><span>│                     单片机 STC15F2K60S2                     │</span></span>
<span class="line"><span>│  ┌─────────────────┐        ┌─────────────────┐             │</span></span>
<span class="line"><span>│  │   P2口          │        │   P0口          │             │</span></span>
<span class="line"><span>│  │ (地址选择)      │        │ (数据输出)      │             │</span></span>
<span class="line"><span>│  │ P2.7=使能       │        │ P0.0-P0.7       │             │</span></span>
<span class="line"><span>│  │ P2.6~P2.4=地址  │        │                 │             │</span></span>
<span class="line"><span>│  └────────┬────────┘        └────────┬────────┘             │</span></span>
<span class="line"><span>│           │                          │                      │</span></span>
<span class="line"><span>│           ▼                          ▼                      │</span></span>
<span class="line"><span>│  ┌─────────────────┐        ┌─────────────────┐             │</span></span>
<span class="line"><span>│  │   74HC138       │        │                 │             │</span></span>
<span class="line"><span>│  │   (译码器)      │        │   74HC573 x 4   │             │</span></span>
<span class="line"><span>│  │  &quot;选择器&quot;       │        │   (锁存器)      │             │</span></span>
<span class="line"><span>│  │                 │        │  &quot;保持器&quot;       │             │</span></span>
<span class="line"><span>│  │  决定选通哪个   ├────────►│  保持外设状态   │             │</span></span>
<span class="line"><span>│  │  74HC573的LE端  │        │                 │             │</span></span>
<span class="line"><span>│  └─────────────────┘        └────────┬────────┘             │</span></span>
<span class="line"><span>│                                      │                      │</span></span>
<span class="line"><span>│                           ┌──────────┼──────────┐          │</span></span>
<span class="line"><span>│                           ▼          ▼          ▼          │</span></span>
<span class="line"><span>│                        ┌────┐    ┌────┐    ┌────┐         │</span></span>
<span class="line"><span>│                        │LED │    │数码管│  │蜂鸣器│       │</span></span>
<span class="line"><span>│                        └────┘    └────┘    └────┘         │</span></span>
<span class="line"><span>└────────────────────────────────────────────────────────────┘</span></span></code></pre></div><hr><h2 id="五、总线操作的核心-为什么必须-关门" tabindex="-1">五、总线操作的核心：为什么必须&quot;关门&quot;？ <a class="header-anchor" href="#五、总线操作的核心-为什么必须-关门" aria-label="Permalink to &quot;五、总线操作的核心：为什么必须&quot;关门&quot;？&quot;">​</a></h2><h3 id="_5-1-错误示范-数据污染" tabindex="-1">5.1 错误示范（数据污染） <a class="header-anchor" href="#_5-1-错误示范-数据污染" aria-label="Permalink to &quot;5.1 错误示范（数据污染）&quot;">​</a></h3><div class="language-c vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">c</span><pre class="shiki shiki-themes github-light one-dark-pro vp-code" tabindex="0"><code><span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">// ❌ 错误写法：不关Y4就操作Y5</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">void</span><span style="--shiki-light:#6F42C1;--shiki-dark:#61AFEF;"> Wrong_Control</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">(</span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">void</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">)</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">{</span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // [步骤1] 打开Y4通道，让LED全亮</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P2 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;"> 0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">80</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">      // Y4选通，LED锁存器的LE=高电平（直通模式）</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P0 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;"> 0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">00</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">      // LED全亮 (低电平亮)</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">  </span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // [错误] 没有执行 P2 = 0x00 关闭Y4！</span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // 此时LED锁存器的LE仍然是高电平，处于&quot;直通模式&quot;</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">  </span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // [步骤2] 打开Y5通道，想控制蜂鸣器</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P2 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;"> 0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">A0</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">      // Y5选通</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P0 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;"> 0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">40</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">      // 意图：蜂鸣器响</span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // 但是！！由于Y4没关，P0=0x40也传到了LED锁存器！</span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // LED状态变成了 0x40 (0100 0000)，大部分熄灭！</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">}</span></span></code></pre></div><p><strong>问题本质</strong>：74HC573的LE=高电平时是<strong>直通模式</strong>，P0的任何变化会实时反映到输出端。</p><h3 id="_5-2-正确示范-先关门" tabindex="-1">5.2 正确示范（先关门） <a class="header-anchor" href="#_5-2-正确示范-先关门" aria-label="Permalink to &quot;5.2 正确示范（先关门）&quot;">​</a></h3><div class="language-c vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">c</span><pre class="shiki shiki-themes github-light one-dark-pro vp-code" tabindex="0"><code><span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">// ✅ 正确写法：操作完一个外设必须关门</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">void</span><span style="--shiki-light:#6F42C1;--shiki-dark:#61AFEF;"> BusWrite</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">(</span><span style="--shiki-light:#24292E;--shiki-dark:#E06C75;">u8 </span><span style="--shiki-light:#E36209;--shiki-light-font-style:inherit;--shiki-dark:#E06C75;--shiki-dark-font-style:italic;">addr</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">,</span><span style="--shiki-light:#24292E;--shiki-dark:#E06C75;"> u8 </span><span style="--shiki-light:#E36209;--shiki-light-font-style:inherit;--shiki-dark:#E06C75;--shiki-dark-font-style:italic;">dat</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">)</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">{</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P2 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;"> addr;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">      // [1] 选通对应锁存器，LE=高电平</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P0 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;"> dat;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">       // [2] 输出数据</span></span>
<span class="line"><span style="--shiki-light:#6F42C1;--shiki-dark:#61AFEF;">    _nop_</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">();</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">        // [3] 短暂延时，确保数据稳定</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P2 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;"> 0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">00</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">      // [4] 关闭锁存器，LE=低电平，数据被&quot;锁存&quot;住</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">}</span></span>
<span class="line"></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">void</span><span style="--shiki-light:#6F42C1;--shiki-dark:#61AFEF;"> Correct_Control</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">(</span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">void</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">)</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">{</span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // 控制LED全亮</span></span>
<span class="line"><span style="--shiki-light:#6F42C1;--shiki-dark:#61AFEF;">    BusWrite</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">(</span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;">0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">80</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">, </span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;">0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">00</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">);</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">   // Y4通道，LED全亮，关门后LED状态被保持</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">  </span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // 控制蜂鸣器</span></span>
<span class="line"><span style="--shiki-light:#6F42C1;--shiki-dark:#61AFEF;">    BusWrite</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">(</span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;">0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">A0</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">, </span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;">0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">40</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">);</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">   // Y5通道，蜂鸣器响，不影响LED！</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">  </span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // LED保持全亮，不受后续P0操作影响 ✓</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">}</span></span></code></pre></div><h3 id="_5-3-时序图对比" tabindex="-1">5.3 时序图对比 <a class="header-anchor" href="#_5-3-时序图对比" aria-label="Permalink to &quot;5.3 时序图对比&quot;">​</a></h3><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light one-dark-pro vp-code" tabindex="0"><code><span class="line"><span>【错误操作 - Y4没关就开Y5，导致污染】</span></span>
<span class="line"><span></span></span>
<span class="line"><span>时间轴 ──────────────────────────────────────────────────►</span></span>
<span class="line"><span></span></span>
<span class="line"><span>P2:     0x80              0xA0</span></span>
<span class="line"><span>        │                 │</span></span>
<span class="line"><span>        ▼                 ▼</span></span>
<span class="line"><span>      打开Y4            打开Y5</span></span>
<span class="line"><span>        │                 │</span></span>
<span class="line"><span>        │   P0=0x00       │     P0=0x40</span></span>
<span class="line"><span>        │      │          │        │</span></span>
<span class="line"><span>        ▼      ▼          ▼        ▼</span></span>
<span class="line"><span>      Y4直通  LED亮     Y5直通    P0=0x40</span></span>
<span class="line"><span>        │    (直通)    Y4仍开着!   │</span></span>
<span class="line"><span>        │      │          │        ▼</span></span>
<span class="line"><span>        │      │          │      LED也收到0x40!</span></span>
<span class="line"><span>        │      │          │        │</span></span>
<span class="line"><span>        └──────┴──────────┴────────┘</span></span>
<span class="line"><span>                          </span></span>
<span class="line"><span>LED状态: 亮 ──────► 灭 ✗ (被污染)</span></span>
<span class="line"><span></span></span>
<span class="line"><span></span></span>
<span class="line"><span>【正确操作 - 先关Y4再开Y5，无影响】</span></span>
<span class="line"><span></span></span>
<span class="line"><span>时间轴 ──────────────────────────────────────────────────►</span></span>
<span class="line"><span></span></span>
<span class="line"><span>P2:     0x80              0x00              0xA0</span></span>
<span class="line"><span>        │                 │                 │</span></span>
<span class="line"><span>        ▼                 ▼                 ▼</span></span>
<span class="line"><span>      打开Y4            关闭Y4            打开Y5</span></span>
<span class="line"><span>        │               (锁存)              │</span></span>
<span class="line"><span>        │   P0=0x00       │       P0=0x40   │</span></span>
<span class="line"><span>        │      │          │          │      │</span></span>
<span class="line"><span>        ▼      ▼          ▼          ▼      ▼</span></span>
<span class="line"><span>      Y4直通  LED亮     Y4锁存     Y5直通   蜂鸣器响</span></span>
<span class="line"><span>        │    (直通)    (保持亮)    (Y4无关)  │</span></span>
<span class="line"><span>        │      │          │          │      │</span></span>
<span class="line"><span>        └──────┴──────────┴──────────┴──────┘</span></span>
<span class="line"><span>                          </span></span>
<span class="line"><span>LED状态: 亮 ──────► 保持亮 ✓ (不被污染)</span></span></code></pre></div><hr><h2 id="六、总线操作四步法则" tabindex="-1">六、总线操作四步法则 <a class="header-anchor" href="#六、总线操作四步法则" aria-label="Permalink to &quot;六、总线操作四步法则&quot;">​</a></h2><h3 id="_6-1-必须遵循的时序" tabindex="-1">6.1 必须遵循的时序 <a class="header-anchor" href="#_6-1-必须遵循的时序" aria-label="Permalink to &quot;6.1 必须遵循的时序&quot;">​</a></h3><div class="language- vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang"></span><pre class="shiki shiki-themes github-light one-dark-pro vp-code" tabindex="0"><code><span class="line"><span>┌────────────────────────────────────────────────────────────┐</span></span>
<span class="line"><span>│                   正确的总线写入时序                         │</span></span>
<span class="line"><span>├────────────────────────────────────────────────────────────┤</span></span>
<span class="line"><span>│                                                            │</span></span>
<span class="line"><span>│  步骤1: 选通                                                │</span></span>
<span class="line"><span>│    P2 = addr;    // 选通对应锁存器，LE=高电平               │</span></span>
<span class="line"><span>│                  // 74HC573进入&quot;直通模式&quot;                    │</span></span>
<span class="line"><span>│                                                            │</span></span>
<span class="line"><span>│  步骤2: 写入                                                │</span></span>
<span class="line"><span>│    P0 = dat;     // 数据输出到P0口                          │</span></span>
<span class="line"><span>│                  // 由于LE=高，数据直通到Q端                │</span></span>
<span class="line"><span>│                                                            │</span></span>
<span class="line"><span>│  步骤3: 延时                                                │</span></span>
<span class="line"><span>│    _nop_();      // 短暂延时(约1us)                         │</span></span>
<span class="line"><span>│                  // 确保数据稳定建立                        │</span></span>
<span class="line"><span>│                                                            │</span></span>
<span class="line"><span>│  步骤4: 关闭（关键！）                                       │</span></span>
<span class="line"><span>│    P2 = 0x00;    // 关闭锁存器，LE=低电平                   │</span></span>
<span class="line"><span>│                  // 74HC573进入&quot;锁存模式&quot;                   │</span></span>
<span class="line"><span>│                  // Q端保持最后状态，不再跟随D端            │</span></span>
<span class="line"><span>│                                                            │</span></span>
<span class="line"><span>│  关键：必须执行步骤4！否则后续P0操作会&quot;污染&quot;该外设         │</span></span>
<span class="line"><span>│                                                            │</span></span>
<span class="line"><span>└────────────────────────────────────────────────────────────┘</span></span></code></pre></div><h3 id="_6-2-直观比喻" tabindex="-1">6.2 直观比喻 <a class="header-anchor" href="#_6-2-直观比喻" aria-label="Permalink to &quot;6.2 直观比喻&quot;">​</a></h3><p>想象你在给不同的房间（外设）送快递（数据）：</p><ul><li><strong>P2口</strong> = 房间门牌号选择器</li><li><strong>P0口</strong> = 传送带，所有房间共享</li><li><strong>74HC573的LE</strong> = 房间门 <ul><li>LE=高 = 门开着（直通），传送带上的东西会直接掉进去</li><li>LE=低 = 门关着（锁存），之前放进去的东西被保存，不受传送带影响</li></ul></li></ul><p><strong>错误操作</strong>：去LED房间送完货不关门，然后去蜂鸣器房间送货，结果传送带上的货也掉进了LED房间！</p><p><strong>正确操作</strong>：每送完一个房间，必须先关门（P2=0x00），再去下一个房间。</p><hr><h2 id="七、常见错误与排查" tabindex="-1">七、常见错误与排查 <a class="header-anchor" href="#七、常见错误与排查" aria-label="Permalink to &quot;七、常见错误与排查&quot;">​</a></h2><h3 id="_7-1-现象与原因对照" tabindex="-1">7.1 现象与原因对照 <a class="header-anchor" href="#_7-1-现象与原因对照" aria-label="Permalink to &quot;7.1 现象与原因对照&quot;">​</a></h3><table tabindex="0"><thead><tr><th>现象</th><th>可能原因</th></tr></thead><tbody><tr><td>LED乱跳/不受控制</td><td>操作其他外设后没有 <code>P2=0x00</code>关门</td></tr><tr><td>数码管有残影/重影</td><td>动态扫描时没有在切换位选前消影</td></tr><tr><td>蜂鸣器不响</td><td>地址写错（写成了0x80而不是0xA0）</td></tr><tr><td>LED状态反了</td><td>逻辑搞反，CT107D是<strong>低电平亮</strong></td></tr><tr><td>所有外设都不工作</td><td>跳线帽没接好（J2/J5等）</td></tr></tbody></table><h3 id="_7-2-调试建议" tabindex="-1">7.2 调试建议 <a class="header-anchor" href="#_7-2-调试建议" aria-label="Permalink to &quot;7.2 调试建议&quot;">​</a></h3><div class="language-c vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">c</span><pre class="shiki shiki-themes github-light one-dark-pro vp-code" tabindex="0"><code><span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">// 在BusWrite中加入调试输出（仅在调试时用）</span></span>
<span class="line"><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">void</span><span style="--shiki-light:#6F42C1;--shiki-dark:#61AFEF;"> BusWrite</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">(</span><span style="--shiki-light:#24292E;--shiki-dark:#E06C75;">u8 </span><span style="--shiki-light:#E36209;--shiki-light-font-style:inherit;--shiki-dark:#E06C75;--shiki-dark-font-style:italic;">addr</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">,</span><span style="--shiki-light:#24292E;--shiki-dark:#E06C75;"> u8 </span><span style="--shiki-light:#E36209;--shiki-light-font-style:inherit;--shiki-dark:#E06C75;--shiki-dark-font-style:italic;">dat</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">)</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">{</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P2 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;"> addr;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">      // 选通</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P0 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;"> dat;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">       // 写入</span></span>
<span class="line"><span style="--shiki-light:#6F42C1;--shiki-dark:#61AFEF;">    _nop_</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">();</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">    P2 </span><span style="--shiki-light:#D73A49;--shiki-dark:#C678DD;">=</span><span style="--shiki-light:#D73A49;--shiki-dark:#E06C75;"> 0x</span><span style="--shiki-light:#005CC5;--shiki-dark:#D19A66;">00</span><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">;</span><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">      // 关门 - 这行必须有！</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">  </span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // 调试时可加延时观察</span></span>
<span class="line"><span style="--shiki-light:#6A737D;--shiki-light-font-style:inherit;--shiki-dark:#7F848E;--shiki-dark-font-style:italic;">    // Delay_ms(100);</span></span>
<span class="line"><span style="--shiki-light:#24292E;--shiki-dark:#ABB2BF;">}</span></span></code></pre></div><hr><h2 id="八、快速参考卡" tabindex="-1">八、快速参考卡 <a class="header-anchor" href="#八、快速参考卡" aria-label="Permalink to &quot;八、快速参考卡&quot;">​</a></h2><h3 id="_8-1-地址速查表" tabindex="-1">8.1 地址速查表 <a class="header-anchor" href="#_8-1-地址速查表" aria-label="Permalink to &quot;8.1 地址速查表&quot;">​</a></h3><table tabindex="0"><thead><tr><th>宏定义</th><th>值</th><th>外设</th><th>控制位</th><th>有效电平</th></tr></thead><tbody><tr><td><code>LATCH_LED</code></td><td>0x80</td><td>LED</td><td>P0.0-P0.7</td><td>0=亮, 1=灭</td></tr><tr><td><code>LATCH_ULN</code></td><td>0xA0</td><td>蜂鸣器/继电器</td><td>P0.6/P0.4</td><td>1=动作</td></tr><tr><td><code>LATCH_DIG</code></td><td>0xC0</td><td>数码管位选</td><td>P0.0-P0.7</td><td>1=选通</td></tr><tr><td><code>LATCH_SEG</code></td><td>0xE0</td><td>数码管段选</td><td>P0.0-P0.7</td><td>0=亮</td></tr></tbody></table><h3 id="_8-2-控制真值表" tabindex="-1">8.2 控制真值表 <a class="header-anchor" href="#_8-2-控制真值表" aria-label="Permalink to &quot;8.2 控制真值表&quot;">​</a></h3><p><strong>LED控制 (地址0x80)</strong></p><table tabindex="0"><thead><tr><th>P0值</th><th>LED状态</th></tr></thead><tbody><tr><td>0x00</td><td>全亮</td></tr><tr><td>0xFF</td><td>全灭</td></tr><tr><td>0xFE</td><td>只有L1亮</td></tr><tr><td>0xAA</td><td>L1,L3,L5,L7亮</td></tr></tbody></table><p><strong>蜂鸣器/继电器控制 (地址0xA0)</strong></p><table tabindex="0"><thead><tr><th>P0值</th><th>bit6</th><th>bit4</th><th>蜂鸣器</th><th>继电器</th></tr></thead><tbody><tr><td>0x00</td><td>0</td><td>0</td><td>关闭</td><td>断开</td></tr><tr><td>0x40</td><td>1</td><td>0</td><td>鸣叫</td><td>断开</td></tr><tr><td>0x10</td><td>0</td><td>1</td><td>关闭</td><td>吸合</td></tr><tr><td>0x50</td><td>1</td><td>1</td><td>鸣叫</td><td>吸合</td></tr></tbody></table><hr><h2 id="九、总结" tabindex="-1">九、总结 <a class="header-anchor" href="#九、总结" aria-label="Permalink to &quot;九、总结&quot;">​</a></h2><h3 id="核心要点" tabindex="-1">核心要点 <a class="header-anchor" href="#核心要点" aria-label="Permalink to &quot;核心要点&quot;">​</a></h3><ol><li><strong>74HC138是&quot;选择器&quot;</strong>：负责根据P2口的地址，选通对应的74HC573锁存器</li><li><strong>74HC573是&quot;保持器&quot;</strong>：负责记住P0口的数据并保持输出给外设</li><li><strong>必须&quot;关门&quot;</strong>：操作完任何外设后，必须执行 <code>P2=0x00</code>关闭锁存器，否则后续P0操作会污染该外设</li><li><strong>四步操作法</strong>：选通→写入→延时→关闭，缺一不可</li></ol><h3 id="记忆口诀" tabindex="-1">记忆口诀 <a class="header-anchor" href="#记忆口诀" aria-label="Permalink to &quot;记忆口诀&quot;">​</a></h3><blockquote><p><strong>138选通道，573保状态，操作完要关门，否则互相害！</strong></p></blockquote><hr><p><em>文档基于 CT107D 开发板原理图编写</em><em>适用于蓝桥杯单片机竞赛学习</em></p><h2 id="文件历史" tabindex="-1">文件历史 <a class="header-anchor" href="#文件历史" aria-label="Permalink to &quot;文件历史&quot;">​</a></h2>`,69)),t(p)])}const D=l(k,[["render",o]]);export{b as __pageData,D as default};
