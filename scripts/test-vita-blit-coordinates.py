"""Exercise production surfaceBlit against a mock of VitaGL's FBO coordinates."""
from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parents[1]
s=(root/'src/butterscotch/src/gl_common/gl_common.c').read_text()
if '--baseline' in sys.argv:
    s=next((root/'artifacts/Backups').glob('v0.77-3_*/src/butterscotch/src/gl_common/gl_common.c')).read_text()
a=s.index('void GLCommon_surfaceBlit('); b=s.index('static bool surfaceReadNeedsFinish',a)
fn=s[a:b]
out=root/'local-test/logs/blit-coordinates'; out.mkdir(parents=True,exist_ok=True)
prefix=r'''
#include <stdint.h>
#include <stdbool.h>
#define assert(x) do { if (!(x)) return __LINE__; } while(0)
#define PLATFORM_VITA 1
#define MAYBE_UNUSED
#define GL_DRAW_FRAMEBUFFER_BINDING 1
#define GL_SCISSOR_TEST 2
#define GL_READ_FRAMEBUFFER 3
#define GL_DRAW_FRAMEBUFFER 4
#define GL_COLOR_BUFFER_BIT 5
#define GL_NEAREST 6
typedef unsigned GLuint;
typedef bool GLboolean;
static int coords[8];
static bool resolveSurfaceFBO(GLuint *s,int32_t*w,int32_t*h,uint32_t n,int32_t id,GLuint*f,int32_t*fw,int32_t*fh){if(id<0||id>=n)return false;*f=s[id];*fw=w[id];*fh=h[id];return true;}
static void glGetIntegerv(int x,int*y){*y=0;}
static bool glIsEnabled(int x){return false;}
static void glDisable(int x){}
static void glEnable(int x){}
static void glBindFramebuffer(int x,int y){}
static void glBlitFramebuffer(int a,int b,int c,int d,int e,int f,int g,int h,int m,int n){int v[]={a,b,c,d,e,f,g,h};for(int i=0;i<8;i++)coords[i]=v[i];}
'''
suffix=r'''
int main(void){
GLuint s[]={1,2};int32_t w[]={640,128},h[]={480,96};
GLCommon_surfaceBlit(s,w,h,2,1,3,5,0,80,48,64,32,true);
// Decode the emitted rectangles using VitaGL's own height-y convention.
assert(coords[0]==80 && 480-coords[1]==48);
assert(coords[2]==144 && 480-coords[3]==80);
assert(coords[4]==3 && 96-coords[5]==5);
assert(coords[6]==67 && 96-coords[7]==37);
GLCommon_surfaceBlit(s,w,h,2,0,0,0,1,0,0,0,0,false);
assert(coords[0]==0 && 96-coords[1]==0);
assert(coords[2]==128 && 96-coords[3]==96);
assert(480-coords[5]==0 && 480-coords[7]==96);
return 0;}
'''
(out/'test.c').write_text(prefix+fn+suffix)
vs=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat')
(out/'run.cmd').write_text('@echo off\ncall "'+str(vs)+'" -arch=x64 >nul\ncl /nologo /TC test.c /Fe:test.exe >build.log 2>&1\nif errorlevel 1 exit /b 1\ntest.exe\n')
r=subprocess.run(['cmd','/c',str(out/'run.cmd')],cwd=out)
if r.returncode: print((out/'build.log').read_text(errors='replace'))
assert r.returncode==0
print('VitaGL asymmetric crop and full-copy coordinates: PASS')
