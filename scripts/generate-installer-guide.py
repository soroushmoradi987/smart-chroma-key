#!/usr/bin/env python3
"""Generate the self-contained bilingual guide shipped inside both plugins."""
import base64
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / 'buildspec.json').read_text())['version']
logo = base64.b64encode((root / '.github/scripts/resources/branding/logo.png').read_bytes()).decode()
html = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Chroma Key — Setup guide</title><style>
*{box-sizing:border-box}body{margin:0;background:#f3f7f8;color:#16303b;font:16px/1.65 system-ui,-apple-system,"Segoe UI",Tahoma,sans-serif}
main{max-width:900px;margin:40px auto;padding:0 24px 40px}header{display:flex;gap:20px;align-items:center;margin-bottom:28px}
header img{width:88px;height:88px}h1{margin:0;font-size:30px;line-height:1.2}header p{margin:6px 0;color:#52707b}
nav{margin:16px 0}nav a{color:#087c65;font-weight:600;margin-inline-end:20px}section{background:white;border:1px solid #dbe6e9;border-radius:16px;padding:28px 32px;margin:20px 0}
h2{font-size:23px;margin:0 0 16px}h3{font-size:18px;margin:24px 0 8px}li{margin:8px 0}code{font:13px/1.7 ui-monospace,Consolas,monospace;overflow-wrap:anywhere;direction:ltr;unicode-bidi:embed}
.note{border-inline-start:4px solid #16b985;background:#effbf5;padding:14px 18px;border-radius:4px}.muted{color:#52707b}.ltr{direction:ltr;unicode-bidi:embed}
table{border-collapse:collapse;width:100%;font-size:14px}th,td{text-align:start;border-bottom:1px solid #dbe6e9;padding:12px;vertical-align:top}
[lang=fa]{line-height:1.9}footer{color:#52707b;font-size:13px;text-align:center}a{color:#087c65}@media(max-width:600px){main{padding:0 12px}section{padding:20px}h1{font-size:24px}header img{width:64px;height:64px}}
</style></head><body><main>
<header><img alt="Smart Chroma Key logo" src="data:image/png;base64,@LOGO@"><div><h1>Smart Chroma Key</h1><p>Version @VERSION@ · Classic + AI · OBS Studio</p></div></header>
<nav><a href="#english">English guide</a><a href="#persian" lang="fa">راهنمای فارسی</a></nav>
<section id="english"><h2>Set up your camera</h2>
<ol><li>Quit OBS before installing. Run the installer for your operating system and follow the steps.</li>
<li>Start OBS Studio 31 or later. Right-click your camera source and choose <b>Filters</b>.</li>
<li>Under <b>Effect Filters</b>, press <b>+</b> and select <b>Smart Chroma Key</b>.</li>
<li>Choose a <b>Processing method</b> using the guidance below. Each method keeps its own settings.</li></ol>
<h3>Classic — for a green or blue screen</h3><p>Choose <b>Smart Chroma Key (Classic)</b>. Set <b>Screen Type</b> to <b>Auto Detect</b> and press <b>Analyze Screen Now</b>. Light the screen evenly and keep the subject away from it. Adjust hair, edge and spill controls gradually while inspecting your actual scene.</p>
<h3>AI — person background removal</h3><p>Choose <b>AI - Robust Video Matting (RVM)</b>. The model is included and runs on your computer. Allow a few seconds for the first result. Select the screen color if you use a green or blue backdrop; otherwise choose <b>None</b>. Increase <b>Halo cleanup</b> only as needed: strong cleanup can remove fine hair.</p>
<p class="note">Set <b>Preview background</b> to <b>Transparent</b> for normal use. White, Black and Alpha matte are inspection views, not the background of your final scene.</p>
<table><tr><th>macOS</th><th>Windows</th></tr><tr><td>Universal plugin for Apple silicon and Intel. AI uses Core ML. Classic also offers Apple Vision person and hair protection.</td><td>64-bit Windows 10/11. AI runs on the CPU. Start at <b>640 - balanced</b>; choose 320 for a slower CPU or 960/1280 for more detail. Higher resolution can lower the AI frame rate.</td></tr></table>
<h3>Installation locations</h3><p>Mac (your account):<br><code>~/Library/Application Support/obs-studio/plugins/smart-chroma-key.plugin</code></p><p>Windows (all users):<br><code>C:\\ProgramData\\obs-studio\\plugins\\smart-chroma-key</code></p>
<h3>If the filter is missing or unchanged</h3><p>Restart OBS normally, outside Safe Mode. Check <b>Effect Filters</b>, not the Sources list. Avoid keeping old copies of this plugin in other OBS plugin folders. Use <b>Help → Log Files → View Current Log</b> and search for <b>Smart Chroma Key</b>. Send the plugin version, OBS version and log with a support request.</p>
<h3>Remove the plugin</h3><p>Quit OBS. On Windows, use <b>Settings → Apps → Smart Chroma Key → Uninstall</b>. On Mac, move only <code>smart-chroma-key.plugin</code> from the folder above to the Trash. Your OBS scenes are not removed.</p>
<p class="muted">No account or online AI service is required. Camera frames stay on your computer. Real-camera results and speed depend on lighting, footage and hardware.</p></section>
<section id="persian" lang="fa" dir="rtl"><h2>راه‌اندازی دوربین</h2>
<ol><li>پیش از نصب، OBS را کامل ببندید. نصب‌کنندهٔ سیستم‌عامل خود را اجرا کنید و مراحل را ادامه دهید.</li>
<li>OBS نسخهٔ ۳۱ یا جدیدتر را باز کنید؛ روی منبع دوربین راست‌کلیک کنید و <b class="ltr">Filters</b> را بزنید.</li>
<li>در بخش <b class="ltr">Effect Filters</b> روی <b>+</b> بزنید و <b class="ltr">Smart Chroma Key</b> را اضافه کنید.</li>
<li>روش پردازش را از <b class="ltr">Processing method</b> انتخاب کنید. تنظیمات هر روش جداگانه نگه داشته می‌شود.</li></ol>
<h3>Classic؛ برای پردهٔ سبز یا آبی</h3><p>روش <b class="ltr">Smart Chroma Key (Classic)</b> را انتخاب کنید. <b class="ltr">Screen Type</b> را روی <b class="ltr">Auto Detect</b> بگذارید و <b class="ltr">Analyze Screen Now</b> را بزنید. پرده را یکنواخت نور بدهید و از آن فاصله بگیرید. تنظیمات مو، لبه و حذف بازتاب رنگ را کم‌کم و با مشاهدهٔ تصویر تغییر دهید.</p>
<h3>AI؛ حذف پس‌زمینهٔ فرد</h3><p>گزینهٔ <b class="ltr">AI - Robust Video Matting (RVM)</b> را انتخاب کنید. مدل همراه نصب‌کننده است و روی رایانهٔ خودتان اجرا می‌شود. بار اول چند ثانیه صبر کنید. اگر پردهٔ سبز یا آبی دارید، رنگ آن را انتخاب کنید؛ در غیر این صورت <b class="ltr">None</b> را بگذارید. افزایش زیاد <b class="ltr">Halo cleanup</b> ممکن است تارهای ظریف مو را حذف کند.</p>
<p class="note">برای استفادهٔ عادی، <b class="ltr">Preview background</b> را روی <b class="ltr">Transparent</b> بگذارید. گزینه‌های سفید، سیاه و آلفا برای بررسی لبه‌ها هستند.</p>
<p>در مک، AI از <span class="ltr">Core ML</span> استفاده می‌کند. در ویندوز، AI روی CPU اجرا می‌شود؛ با وضوح ۶۴۰ شروع کنید و برای رایانهٔ ضعیف‌تر ۳۲۰ را انتخاب کنید. وضوح بالاتر جزئیات بیشتری دارد ولی ممکن است سرعت را کم کند. گزینهٔ محافظت <span class="ltr">Apple Vision</span> مخصوص مک است.</p>
<h3>اگر افزونه دیده نمی‌شود</h3><p>OBS را کامل ببندید و دوباره، خارج از حالت امن، باز کنید. افزونه در <b class="ltr">Effect Filters</b> است، نه فهرست منبع‌ها. نسخه‌های تکراری قدیمی را در پوشه‌های دیگر OBS نگه ندارید. برای گزارش خطا، از <b class="ltr">Help → Log Files → View Current Log</b> استفاده کنید و نسخهٔ OBS و افزونه را هم بفرستید.</p>
<h3>حذف افزونه</h3><p>ابتدا OBS را ببندید. در ویندوز از بخش برنامه‌های Settings، افزونه را Uninstall کنید. در مک فقط پوشهٔ <code>smart-chroma-key.plugin</code> را از مسیر نصب بالا به سطل زباله منتقل کنید. صحنه‌های OBS حذف نمی‌شوند.</p>
<p class="muted">حساب کاربری و سرویس آنلاین لازم نیست؛ تصویر دوربین روی رایانهٔ شما می‌ماند. کیفیت و سرعت به نور، تصویر و سخت‌افزار بستگی دارد.</p></section>
<footer>Smart Chroma Key @VERSION@ · Soroush Moradi · <a href="https://dinejahani.com">dinejahani.com</a></footer>
</main></body></html>'''
(root / 'data/setup-guide.html').write_text(html.replace('@LOGO@', logo).replace('@VERSION@', version))
print('Generated data/setup-guide.html')
