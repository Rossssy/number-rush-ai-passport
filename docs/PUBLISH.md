# 发布到 GitHub

## 1. 新建仓库

登录 https://github.com/new ，仓库名可用 `number-rush-ai-passport`，选择 Public（公开）。包内已有 README、LICENSE 和 .gitignore，不需要再次自动生成它们。

## 2. 上传源码

解压 `number-rush-github.zip`，把里面的 README.md、main、components、media、firmware、tools 等内容上传到仓库根目录，而不是只上传 ZIP 文件。新仓库页面点 uploading an existing file；已有仓库点 Add file → Upload files。确认文件后点 Commit changes。

一次网页上传过多文件时分批上传，或使用 GitHub Desktop。所有文件都应来自这个发布包，不要上传整个 Folotoy 工作目录。

## 3. 发布可下载版本

进入仓库的 Releases → Draft a new release / Create a new release。创建标签 `v1.0.0`，标题写“Number Rush：封面与中文提示版”。把 `number-rush-github.zip` 作为附件上传，填写改动和适用硬件，点 Publish release。

推荐说明：开机封面、实体 OK 开始、暂停/继续、完整中文提示；适用于 AI Passport ESP32-C3 / 8MB。配图为界面示意图，不是实机截图。明确本设备版与网页版规则并未完全同步。

## 4. 检查发布结果

确认 README 中三张图片可显示、ZIP 可下载、源码中 main/ 下的字体和封面资源齐全。邀请别人刷机前，先在自己的设备上测试开机封面、开始、移动、暂停、继续和失败后重开。

GitHub Pages 不能直接运行 ESP32 固件；本仓库是设备游戏的源码和下载页。

## 官方帮助

- https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-new-repository
- https://docs.github.com/en/repositories/working-with-files/managing-files/adding-a-file-to-a-repository
- https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository
