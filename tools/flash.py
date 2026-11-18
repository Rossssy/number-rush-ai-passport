"""Public release flasher; no device-specific identity or credentials."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--port')
    parser.add_argument('--check-only',action='store_true')
    args=parser.parse_args()
    manifest=json.loads((ROOT/'firmware/manifest.json').read_text(encoding='utf-8-sig'))
    segments=[]
    for item in manifest['files']:
        path=ROOT/'firmware'/item['name']
        data=path.read_bytes()
        if len(data)!=item['size'] or hashlib.sha256(data).hexdigest()!=item['sha256']:
            raise RuntimeError('Firmware verification failed: '+item['name'])
        segments.extend([item['address'],str(path)])
    print('All firmware files verified.')
    if args.check_only:return
    if not args.port:parser.error('Specify --port with your actual USB port.')
    command=[sys.executable,'-m','esptool','--chip','esp32c3','-p',args.port]
    subprocess.run(command+['read_mac'],check=True)
    if input('Verify device above. Replace its current firmware? Type YES: ')!='YES':return
    subprocess.run(command+['-b','460800','--before','default_reset','--after','hard_reset',
        'write_flash','--flash_mode','dio','--flash_size','8MB','--flash_freq','80m']+segments,check=True)

if __name__=='__main__':
    try:main()
    except Exception as error:
        print('ERROR:',error,file=sys.stderr)
        sys.exit(1)
