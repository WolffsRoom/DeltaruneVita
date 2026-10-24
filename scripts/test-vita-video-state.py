from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source_path = root / 'src' / 'vita-probe' / 'source' / 'vita_video_draw_gl.h'
source = source_path.read_text(encoding='utf-8')

m = re.search(r'void\s+VitaVideo_drawGL\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}', source, re.S)
assert m, 'VitaVideo_draw body not found'
body = m.group('body')

bind = body.find('glBindTexture(GL_TEXTURE_2D, frameTexture)')
force_unit0 = body.find('glActiveTexture(GL_TEXTURE0)')
assert bind >= 0, 'video frame texture bind missing'
assert force_unit0 >= 0 and force_unit0 < bind, 'VitaVideo_draw must force GL_TEXTURE0 before binding the decoded frame'

assert 'GL_ACTIVE_TEXTURE' in body, 'VitaVideo_draw must save the active texture unit'
assert re.search(r'glActiveTexture\s*\(\s*\(GLenum\)\s*previousActiveTexture\s*\)', body), 'VitaVideo_draw must restore the previous active texture unit'
assert 'GL_CURRENT_PROGRAM' in body and 'glUseProgram((GLuint)previousProgram)' in body, 'VitaVideo_draw must restore the previous shader program'
assert 'GL_TEXTURE_BINDING_2D' in body, 'VitaVideo_draw must save texture unit 0 binding'
assert 'glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture2D)' in body, 'VitaVideo_draw must restore texture unit 0 binding'

assert 'firstLoad ? texcoords_first_load : texcoords_yoyo' in body, 'Only the host first-load movie may use first-load UV orientation; GML chapter videos must use YoYo UVs'
print('PASS: source contract only; run local-test/test_ch5_fixes.py for pixel/state assertions')
