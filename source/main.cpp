/* Midnight Circuit: an original Nintendo DS city-driving game. */
#include <nds.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "font8.h"
#include "song_bin.h"

static const int SW=256, SH=192;
static const float PI=3.14159265f, WORLD=24.0f;
static const float CAMERA_HEIGHT=12.5f, CAMERA_BACK=5.2f;
static u16 hud[SW*SH], *hudVram;
static int frame=0;

enum Mode { TITLE, PLAY, PAUSE, OVER };
enum EndReason { WRECKED, OUT_OF_TIME, BUSTED };
static Mode mode=TITLE;
static EndReason endReason=WRECKED;
struct Point { float x,z; };
struct Vehicle { float x,z,heading,speed; bool police,contactLatch; int tint,junctionWait,pushPause; };
static Vehicle traffic[8];
static const Point stops[]={{8,0},{-16,-8},{16,16},{-8,16},{0,-24},{16,-16},{-24,8},{8,24}};
static const int stopCount=sizeof stops/sizeof stops[0];
static float px=0,pz=0,camX=0,camZ=0,heading=0,speed=0;
static float boxedX=0,boxedZ=0;
static float health=100,heat=0,nitro=100;
static int cash=0,runs=0,missionFrames=70*60,collisionCooldown=0,boostTouch=0,carClass=0,boxedFrames=0;
static bool carrying=false,customWaypoint=false,soundOn=true,timerActive=false;
static Point target=stops[0],personal={0,0};
static int musicSound=-1,eventFlash=0,eventKind=0;

static float clampf(float a,float lo,float hi){return std::max(lo,std::min(hi,a));}
static float length(float x,float z){return sqrtf(x*x+z*z);}
static u16 shade(int r,int g,int b){return RGB15(r,g,b)|BIT(15);}
static void put(int x,int y,u16 c){if((unsigned)x<SW&&(unsigned)y<SH)hud[y*SW+x]=c;}
static void rect(int x,int y,int w,int h,u16 c){
    if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
    w=std::min(w,SW-x);h=std::min(h,SH-y);
    if(w<=0||h<=0)return;
    for(int yy=0;yy<h;++yy){u16 *row=&hud[(y+yy)*SW+x];for(int xx=0;xx<w;++xx)row[xx]=c;}
}
static void circle(int x,int y,int r,u16 c){
    for(int yy=-r;yy<=r;++yy)for(int xx=-r;xx<=r;++xx)
        if(xx*xx+yy*yy<=r*r)put(x+xx,y+yy,c);
}
static void pill(int x,int y,int w,int h,int r,u16 c){
    rect(x+r,y,w-2*r,h,c);rect(x,y+r,w,h-2*r,c);
    for(int yy=0;yy<r;++yy)for(int xx=0;xx<r;++xx)
        if((xx-r)*(xx-r)+(yy-r)*(yy-r)<=r*r){
            put(x+xx,y+yy,c);put(x+w-1-xx,y+yy,c);
            put(x+xx,y+h-1-yy,c);put(x+w-1-xx,y+h-1-yy,c);
        }
}
static void line(int x0,int y0,int x1,int y1,u16 c){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    while(true){put(x0,y0,c);if(x0==x1&&y0==y1)break;int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
static void glyph(int x,int y,unsigned char ch,u16 c,int scale=1){
    if(ch<32||ch>127)ch='?';
    for(int yy=0;yy<8;++yy)for(int xx=0;xx<8;++xx)
        if(font8[ch-32][yy]&(1<<(7-xx)))rect(x+xx*scale,y+yy*scale,scale,scale,c);
}
static void text(int x,int y,const char*s,u16 c,int scale=1,int max=40){
    for(int i=0;s[i]&&i<max&&x+(i+1)*8*scale<=SW;++i)glyph(x+i*8*scale,y,(unsigned char)s[i],c,scale);
}

static void eventCue(int kind){eventKind=kind;eventFlash=kind==3?20:85;}
static void updateAudio(){
    if(!soundOn){soundKill(musicSound);musicSound=-1;return;}
    if(musicSound<0)musicSound=soundPlaySample(song_bin,SoundFormat_16Bit,song_bin_size,16000,68,64,true,0);
    soundSetVolume(musicSound,mode==PLAY?68:47);
}

static int hashCell(int x,int z){unsigned a=(unsigned)(x+17)*1103515245u+(unsigned)(z+19)*12345u;return (int)((a^(a>>13))&0x7fff);}
static bool hitsBuilding(float x,float z){
    float bx=floorf(x/8.0f)*8.0f+4.0f,bz=floorf(z/8.0f)*8.0f+4.0f;
    // Include the full car body, even when it is diagonal to the street.
    return fabsf(x-bx)<3.10f&&fabsf(z-bz)<3.10f;
}
static bool blocked(float x,float z){
    if(x<-WORLD-.6f||x>WORLD+.6f||z<-WORLD-.6f||z>WORLD+.6f)return true;
    return hitsBuilding(x,z);
}
struct CarContact { float nx,nz,depth; };
static bool carContact(float ax,float az,float ah,float bx,float bz,float bh,CarContact *contact){
    if(fabsf(ax-bx)>1.7f||fabsf(az-bz)>1.7f)return false;
    const float arx=cosf(ah),arz=sinf(ah),afx=sinf(ah),afz=-cosf(ah);
    const float brx=cosf(bh),brz=sinf(bh),bfx=sinf(bh),bfz=-cosf(bh);
    const float axes[4][2]={{arx,arz},{afx,afz},{brx,brz},{bfx,bfz}};
    float shallowest=1e9f,normalX=0,normalZ=0;
    for(int i=0;i<4;++i){
        float ux=axes[i][0],uz=axes[i][1];
        float spanA=.46f*fabsf(ux*arx+uz*arz)+.72f*fabsf(ux*afx+uz*afz);
        float spanB=.46f*fabsf(ux*brx+uz*brz)+.72f*fabsf(ux*bfx+uz*bfz);
        float offset=(bx-ax)*ux+(bz-az)*uz;
        float overlap=spanA+spanB-fabsf(offset);
        if(overlap<=0)return false;
        if(overlap<shallowest){
            shallowest=overlap;
            normalX=offset>=0?ux:-ux;normalZ=offset>=0?uz:-uz;
        }
    }
    if(contact){contact->nx=normalX;contact->nz=normalZ;contact->depth=shallowest;}
    return true;
}
static bool carsTouch(float ax,float az,float ah,float bx,float bz,float bh){
    return carContact(ax,az,ah,bx,bz,bh,0);
}
static bool trafficClear(int moving,float x,float z){
    Vehicle &v=traffic[moving];
    if(blocked(x,z))return false;
    for(int j=0;j<8;++j)if(j!=moving&&carsTouch(x,z,v.heading,traffic[j].x,traffic[j].z,traffic[j].heading))return false;
    return true;
}
static void initTraffic(){
    for(int i=0;i<8;++i){
        Vehicle &v=traffic[i];v.police=i>=6;v.contactLatch=false;v.tint=i%3;v.junctionWait=0;v.pushPause=0;
        if(i%2==0){v.x=((i%5)-2)*8.0f+(i%4<2?.55f:-.55f);v.z=((i*7)%43)-21.0f;v.heading=i%4<2?0:PI;}
        else{v.z=((i%5)-2)*8.0f+(i%4<2?.55f:-.55f);v.x=((i*9)%43)-21.0f;v.heading=i%4<2?PI/2:-PI/2;}
        v.speed=v.police?.11f:(.065f+.009f*(i%3));
    }
    // Give the player a clear departure from the central garage.
    traffic[2].z=12.0f;traffic[3].x=-12.0f;traffic[7].x=-20.0f;
}
static void resetGame(){
    mode=PLAY;px=pz=camX=camZ=heading=speed=0;health=100;heat=0;nitro=100;
    cash=runs=carClass=0;carrying=false;timerActive=false;target=stops[0];missionFrames=70*60;
    collisionCooldown=boostTouch=boxedFrames=0;boxedX=boxedZ=0;endReason=WRECKED;customWaypoint=false;initTraffic();eventCue(4);
}
static void changeMission(){
    if(!carrying){carrying=true;target=stops[(runs*2+1)%stopCount];heat=std::max(heat,2.3f);missionFrames=65*60;eventCue(0);}
    else{
        carrying=false;++runs;cash+=250+runs*75;health=std::min(100.0f,health+13.0f);
        heat=std::max(0.0f,heat-.8f);nitro=std::min(100.0f,nitro+25.0f);
        target=stops[(runs*2)%stopCount];missionFrames=70*60;eventCue(1);
    }
}
static void hit(float damage){
    if(collisionCooldown>0)return;
    collisionCooldown=35;health-=damage;heat=std::min(5.0f,heat+.55f);
    speed*=-.36f;eventCue(2);
    if(health<=0){health=0;endReason=WRECKED;mode=OVER;}
}
static void hitCar(float damage,float impact){
    if(collisionCooldown>0||impact<.035f)return;
    collisionCooldown=55;health-=damage;heat=std::min(5.0f,heat+.12f);
    speed*=.88f;eventCue(2);
    if(health<=0){health=0;endReason=WRECKED;mode=OVER;}
}
static bool boxedByPolice(){
    const Vehicle &a=traffic[6],&b=traffic[7];
    float ax=a.x-px,az=a.z-pz,bx=b.x-px,bz=b.z-pz;
    float a2=ax*ax+az*az,b2=bx*bx+bz*bz;
    // Both patrol cars must be within a body length and cover different escape directions.
    return a2<4.6f&&b2<4.6f&&ax*bx+az*bz<.35f*sqrtf(a2*b2);
}
static bool pushTraffic(int i,float playerX,float playerZ,float power){
    Vehicle &v=traffic[i];CarContact contact;
    if(!carContact(playerX,playerZ,heading,v.x,v.z,v.heading,&contact))return true;
    float distance=contact.depth+.025f;
    if(distance>power)return false;
    float nx=v.x+contact.nx*distance,nz=v.z+contact.nz*distance;
    if(!trafficClear(i,nx,nz)||carsTouch(playerX,playerZ,heading,nx,nz,v.heading))return false;
    v.x=nx;v.z=nz;v.pushPause=12;v.contactLatch=true;
    return true;
}
static void steerPoliceAtJunction(Vehicle &v){
    if(v.junctionWait>0)return;
    bool horizontal=fabsf(sinf(v.heading))>.5f;
    float movingAxis=horizontal?v.x:v.z;
    float crossing=roundf(movingAxis/8.0f)*8.0f;
    if(fabsf(movingAxis-crossing)>v.speed+.04f)return;
    const float choices[4]={0,PI/2,PI,-PI/2};
    float bestScore=1e9f,bestHeading=v.heading;
    for(int n=0;n<4;++n){
        float direction=choices[n];
        float nx=v.x+sinf(direction)*1.05f,nz=v.z-cosf(direction)*1.05f;
        if(hitsBuilding(nx,nz))continue;
        bool occupied=false;
        for(int j=0;j<8;++j)if(&traffic[j]!=&v&&
            carsTouch(nx,nz,direction,traffic[j].x,traffic[j].z,traffic[j].heading)){
            occupied=true;break;
        }
        if(occupied)continue;
        float turn=fabsf(direction-v.heading);if(turn>PI)turn=2*PI-turn;
        float score=fabsf(px-nx)+fabsf(pz-nz)+turn*.11f;
        if(score<bestScore){bestScore=score;bestHeading=direction;}
    }
    v.heading=bestHeading;
    v.junctionWait=bestScore<1e8f?12:4;
}
static void updateTraffic(){
    for(int i=0;i<8;++i){
        Vehicle &v=traffic[i];
        if(v.pushPause>0){--v.pushPause;continue;}
        if(v.junctionWait>0)--v.junctionWait;
        if(v.police&&heat>.8f){
            v.speed=.075f+heat*.009f;
            steerPoliceAtJunction(v);
        }
        float nx=v.x+sinf(v.heading)*v.speed,nz=v.z-cosf(v.heading)*v.speed;
        if(nx>WORLD+1)nx=-WORLD-1;
        if(nx<-WORLD-1)nx=WORLD+1;
        if(nz>WORLD+1)nz=-WORLD-1;
        if(nz<-WORLD-1)nz=WORLD+1;
        if(hitsBuilding(nx,nz)){v.contactLatch=false;continue;}
        if(carsTouch(nx,nz,v.heading,px,pz,heading)){
            if(!v.contactLatch)hitCar(v.police?2.5f:1.0f,fabsf(v.speed-speed));
            v.contactLatch=true;
            continue;
        }
        v.contactLatch=false;
        bool occupied=false;
        for(int j=0;j<8;++j)if(j!=i&&carsTouch(nx,nz,v.heading,traffic[j].x,traffic[j].z,traffic[j].heading)){
            occupied=true;break;
        }
        if(!occupied){v.x=nx;v.z=nz;}
    }
}
static void updateGame(int held,int pressed,touchPosition touch){
    if(pressed&KEY_SELECT)soundOn=!soundOn;
    if(pressed&KEY_X&&length(px,pz)<2.1f&&cash>=100){
        cash-=100;health=std::min(100.0f,health+55.0f);nitro=100;
        int unlocked=runs>=4?3:runs>=2?2:1;carClass=(carClass+1)%unlocked;eventCue(1);
    }
    bool mapTouch=(held&KEY_TOUCH)&&touch.px<148&&touch.py>=37&&touch.py<166;
    if(mapTouch){
        personal.x=clampf((touch.px-79)/2.45f,-WORLD,WORLD);
        personal.z=clampf((touch.py-100)/2.45f,-WORLD,WORLD);customWaypoint=true;
    }
    if((held&KEY_TOUCH)&&touch.px>=151&&touch.py>=138&&touch.py<166){
        boostTouch=20;
    }
    if(boostTouch>0)--boostTouch;
    bool boost=((held&KEY_L)||boostTouch>0)&&nitro>0.0f;
    float maxSpeed=carClass==1?.30f:carClass==2?.22f:.255f;
    if(mapTouch){
        float dx=personal.x-px,dz=personal.z-pz;
        float delta=atan2f(dx,-dz)-heading;
        while(delta>PI)delta-=2*PI;
        while(delta<-PI)delta+=2*PI;
        heading+=clampf(delta,-.055f,.055f);
        if(length(dx,dz)>1.1f&&fabsf(delta)<.48f)speed+=.0065f;
        else speed*=.94f;
    }
    bool gas=(held&(KEY_A|KEY_UP))!=0;
    bool brake=(held&(KEY_B|KEY_DOWN))!=0;
    if(gas)speed+=boost?.012f:.0065f;
    else if(brake)speed-=.012f;
    else speed*=.982f;
    if(gas&&brake)speed*=.94f;
    if(boost&&fabsf(speed)>.07f){nitro=std::max(0.0f,nitro-.70f);if(frame%38==0)eventCue(3);}
    else nitro=std::min(100.0f,nitro+.075f);
    speed=clampf(speed,-.12f,maxSpeed+(boost?.13f:0));
    int steer=((held&KEY_RIGHT)?1:0)-((held&KEY_LEFT)?1:0);
    if(steer&&fabsf(speed)>.014f)heading+=steer*(speed<0?-1.0f:1.0f)*(.018f+fabsf(speed)*.13f);
    if(heading>PI)heading-=2*PI;
    if(heading<-PI)heading+=2*PI;
    float nx=px+sinf(heading)*speed,nz=pz-cosf(heading)*speed;
    bool blockedByPolice=false;
    if(blocked(nx,nz))hit(5.0f);
    else{
        bool occupied=false;
        for(int i=0;i<8;++i)if(carsTouch(nx,nz,heading,traffic[i].x,traffic[i].z,traffic[i].heading)){
            float impact=fabsf(speed)+fabsf(traffic[i].speed);
            float power=.10f+fabsf(speed)*1.8f+(carClass==2?.10f:0.0f);
            if(fabsf(speed)<.035f||!pushTraffic(i,nx,nz,power)){
                occupied=true;blockedByPolice=traffic[i].police;
            }
            hitCar(traffic[i].police?2.5f:1.0f,impact);
            if(occupied)break;
        }
        if(!occupied){px=nx;pz=nz;}
    }
    // Keep the car near the centre while leaving a little room in front of it.
    float leadX=sinf(heading)*.75f,leadZ=-cosf(heading)*.75f;
    camX+=(px+leadX-camX)*.32f;camZ+=(pz+leadZ-camZ)*.32f;
    if(collisionCooldown>0)--collisionCooldown;
    if(heat>0)heat=std::max(0.0f,heat-.0007f);
    updateTraffic();
    if(mode==PLAY&&boxedByPolice()&&(blockedByPolice||boxedFrames>0)){
        if(boxedFrames==0){boxedX=px;boxedZ=pz;}
        if(length(px-boxedX,pz-boxedZ)<.75f){
            if(++boxedFrames>=90){boxedFrames=90;endReason=BUSTED;mode=OVER;}
        }else boxedFrames=0;
    }else boxedFrames=0;
    if(length(px-target.x,pz-target.z)<1.35f)changeMission();
    if(fabsf(speed)>.025f)timerActive=true;
    if(mode==PLAY&&timerActive&&--missionFrames<=0){missionFrames=0;endReason=OUT_OF_TIME;mode=OVER;}
}

static void glColor5(int r,int g,int b){glColor(RGB15(r,g,b));}
static void flat(float x,float z,float w,float d,float y,int r,int g,int b){
    glColor5(r,g,b);glBegin(GL_QUADS);
    glVertex3f(x,y,z);glVertex3f(x+w,y,z);glVertex3f(x+w,y,z+d);glVertex3f(x,y,z+d);
    glEnd();
}
static void solid(float x,float y,float z,float w,float h,float d,int r,int g,int b){
    float x2=x+w,y2=y+h,z2=z+d;
    glBegin(GL_QUADS);
    glColor5(std::min(31,r+4),std::min(31,g+4),std::min(31,b+4));
    glVertex3f(x,y2,z);glVertex3f(x2,y2,z);glVertex3f(x2,y2,z2);glVertex3f(x,y2,z2);
    glColor5(r,g,b);
    glVertex3f(x,y,z2);glVertex3f(x2,y,z2);glVertex3f(x2,y2,z2);glVertex3f(x,y2,z2);
    glColor5(r*3/4,g*3/4,b*3/4);
    glVertex3f(x2,y,z);glVertex3f(x2,y,z2);glVertex3f(x2,y2,z2);glVertex3f(x2,y2,z);
    glColor5(r*2/3,g*2/3,b*2/3);
    glVertex3f(x,y,z);glVertex3f(x2,y,z);glVertex3f(x2,y2,z);glVertex3f(x,y2,z);
    glColor5(r*4/5,g*4/5,b*4/5);
    glVertex3f(x,y,z);glVertex3f(x,y,z2);glVertex3f(x,y2,z2);glVertex3f(x,y2,z);
    glEnd();
}
static void roadTile(){
    flat(-4,-4,8,8,-.045f,4,8,12);
    flat(-4,-1.19f,8,2.38f,0,6,10,15);
    flat(-1.19f,-4,2.38f,8,.003f,6,10,15);
    for(int i=-3;i<=3;i+=2){
        flat(i-.42f,-.035f,.84f,.07f,.014f,19,17,8);
        flat(-.035f,i-.42f,.07f,.84f,.014f,19,17,8);
    }
    flat(-1.22f,-1.22f,2.44f,.06f,.018f,12,16,20);
    flat(-1.22f,1.16f,2.44f,.06f,.018f,12,16,20);
}
static void building(int k,int l){
    int h=hashCell(k,l);float height=1.45f+(h%5)*.47f;
    int style=h%4;
    static const int colors[4][3]={{12,15,21},{15,12,22},{12,20,22},{18,16,19}};
    int r=colors[style][0],g=colors[style][1],b=colors[style][2];
    flat(-3.05f,-3.05f,6.1f,6.1f,.025f,10,13,17);
    solid(-2.12f,.08f,-2.12f,4.24f,height,4.24f,r,g,b);
    solid(-2.2f,height+.08f,-2.2f,4.4f,.11f,4.4f,style==1?20:8,style==1?7:22,style==1?24:24);
    if(style==2)solid(-1.35f,height+.19f,-1.35f,2.7f,.3f,2.7f,7,19,22);
    int wr=style==1?25:5,wg=style==1?7:27,wb=style==1?25:29;
    for(float yy=.42f;yy<height-.17f;yy+=.43f){
        for(int c=-1;c<=1;++c){
            float xx=c*1.15f;
            flat(xx-.32f,2.138f,.64f,.028f,yy,wr,wg,wb);
            glColor5(wr,wg,wb);glBegin(GL_QUADS);
            glVertex3f(2.139f,yy,xx-.32f);glVertex3f(2.139f,yy,xx+.32f);
            glVertex3f(2.139f,yy+.17f,xx+.32f);glVertex3f(2.139f,yy+.17f,xx-.32f);
            glEnd();
        }
    }
    if(style==1)solid(-.95f,.12f,2.13f,1.9f,.24f,.08f,26,6,22);
}
static void carModel(bool police,int tint,bool player){
    int r=player?(carClass==2?21:5):(police?7:(tint==1?27:tint==2?22:12));
    int g=player?(carClass==2?18:26):(police?10:(tint==1?11:tint==2?17:20));
    int b=player?(carClass==2?10:29):(police?24:(tint==1?9:tint==2?25:27));
    flat(-.49f,-.73f,.98f,1.46f,.012f,2,3,7);
    solid(-.36f,.11f,-.65f,.72f,.28f,1.3f,r,g,b);
    solid(-.28f,.39f,-.21f,.56f,.23f,.55f,8,19,26);
    solid(-.34f,.25f,-.67f,.22f,.08f,.05f,31,30,19);
    solid(.12f,.25f,-.67f,.22f,.08f,.05f,31,30,19);
    solid(-.32f,.25f,.64f,.18f,.07f,.04f,28,4,8);
    solid(.14f,.25f,.64f,.18f,.07f,.04f,28,4,8);
    for(int x=-1;x<=1;x+=2)for(int z=-1;z<=1;z+=2)
        solid(x<0?-.43f:.34f,.015f,z<0?-.46f:.33f,.09f,.17f,.22f,3,4,5);
    if(police){solid(-.21f,.62f,-.04f,.19f,.08f,.16f,30,4,7);solid(.02f,.62f,-.04f,.19f,.08f,.16f,5,12,31);}
    if(player&&nitro>0&&speed>.26f){
        solid(-.21f,.19f,.66f,.15f,.11f,.25f,5,27,31);
        solid(.06f,.19f,.66f,.15f,.11f,.25f,5,27,31);
    }
}
static void faceTravel(float theta){
    // The model's nose is local -Z; libnds positive Y rotation turns it left.
    // Physics uses positive heading to travel right, so negate for rendering.
    glRotateY(-theta*180.0f/PI);
}
static void beacon(Point p,bool orange){
    if(fabsf(p.x-camX)>13||fabsf(p.z-camZ)>13)return;
    glPushMatrix();glTranslatef(p.x-camX,0,p.z-camZ);
    float wave=.12f*sinf(frame*.09f);int r=orange?31:5,g=orange?19:30,b=orange?5:31;
    flat(-.8f,-.8f,1.6f,.08f,.055f,r,g,b);flat(-.8f,.72f,1.6f,.08f,.055f,r,g,b);
    flat(-.8f,-.8f,.08f,1.6f,.055f,r,g,b);flat(.72f,-.8f,.08f,1.6f,.055f,r,g,b);
    glPolyFmt(POLY_ALPHA(22)|POLY_CULL_NONE|POLY_ID(7));
    glColor5(r,g,b);glBegin(GL_TRIANGLES);
    glVertex3f(0,1.22f+wave,0);glVertex3f(-.38f,.49f,-.38f);glVertex3f(.38f,.49f,-.38f);
    glVertex3f(0,1.22f+wave,0);glVertex3f(.38f,.49f,-.38f);glVertex3f(.38f,.49f,.38f);
    glVertex3f(0,1.22f+wave,0);glVertex3f(.38f,.49f,.38f);glVertex3f(-.38f,.49f,.38f);
    glVertex3f(0,1.22f+wave,0);glVertex3f(-.38f,.49f,.38f);glVertex3f(-.38f,.49f,-.38f);
    glEnd();glPolyFmt(POLY_ALPHA(31)|POLY_CULL_NONE|POLY_ID(1));glPopMatrix(1);
}
static bool cutsAcrossCar(float bx,float bz,float height){
    // Hide a building only when its footprint intersects the actual camera-to-car
    // sightline below roof height. The surrounding city stays visible.
    const float sx=camX,sz=camZ+CAMERA_BACK;
    const float dx=px-sx,dz=pz-sz;
    float enter=0.0f,leave=1.0f;
    const float minX=bx-2.65f,maxX=bx+2.65f;
    const float minZ=bz-2.65f,maxZ=bz+2.65f;
    if(fabsf(dx)<.0001f){if(sx<minX||sx>maxX)return false;}
    else{
        float a=(minX-sx)/dx,b=(maxX-sx)/dx;if(a>b)std::swap(a,b);
        enter=std::max(enter,a);leave=std::min(leave,b);
    }
    if(fabsf(dz)<.0001f){if(sz<minZ||sz>maxZ)return false;}
    else{
        float a=(minZ-sz)/dz,b=(maxZ-sz)/dz;if(a>b)std::swap(a,b);
        enter=std::max(enter,a);leave=std::min(leave,b);
    }
    if(enter>leave||leave<0.0f||enter>1.0f)return false;
    float sightHeight=CAMERA_HEIGHT+(0.55f-CAMERA_HEIGHT)*leave;
    return sightHeight<height+.35f;
}
static void renderWorld(){
    glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(54,256.0f/192.0f,.1f,36.0f);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    float sway=mode==TITLE?sinf(frame*.004f)*1.25f:0;
    gluLookAt(sway,CAMERA_HEIGHT,CAMERA_BACK,0,0,0,0,1,0);
    glPolyFmt(POLY_ALPHA(31)|POLY_CULL_NONE|POLY_ID(1));
    for(int k=-3;k<=3;++k)for(int l=-3;l<=3;++l){
        float x=k*8.0f-camX,z=l*8.0f-camZ;
        if(fabsf(x)>15||fabsf(z)>15)continue;
        glPushMatrix();glTranslatef(x,0,z);roadTile();glPopMatrix(1);
    }
    for(int k=-3;k<=2;++k)for(int l=-3;l<=2;++l){
        float x=k*8.0f+4-camX,z=l*8.0f+4-camZ;
        if(fabsf(x)>14||fabsf(z)>14)continue;
        float roof=1.45f+(hashCell(k,l)%5)*.47f+.49f;
        if(mode!=TITLE&&cutsAcrossCar(k*8.0f+4,l*8.0f+4,roof))continue;
        glPushMatrix();glTranslatef(x,0,z);building(k,l);glPopMatrix(1);
    }
    glPushMatrix();glTranslatef(-camX,0,-camZ);
    flat(-.88f,-.88f,1.76f,.1f,.056f,5,28,15);flat(-.88f,.78f,1.76f,.1f,.056f,5,28,15);
    flat(-.88f,-.88f,.1f,1.76f,.056f,5,28,15);flat(.78f,-.88f,.1f,1.76f,.056f,5,28,15);
    glPopMatrix(1);
    beacon(target,!carrying);
    if(customWaypoint)beacon(personal,false);
    for(int i=0;i<8;++i){
        const Vehicle &v=traffic[i];float x=v.x-camX,z=v.z-camZ;
        if(fabsf(x)>12||fabsf(z)>12)continue;
        glPushMatrix();glTranslatef(x,0,z);faceTravel(v.heading);carModel(v.police,v.tint,false);glPopMatrix(1);
    }
    glPushMatrix();glTranslatef(px-camX,0,pz-camZ);faceTravel(heading);carModel(false,0,true);glPopMatrix(1);
    glFlush(0);
}

static int mapX(float x){return 79+(int)(x*2.45f);}
static int mapY(float z){return 100+(int)(z*2.45f);}
static void mapDot(float x,float z,int radius,u16 color){circle(mapX(x),mapY(z),radius,color);}
static void drawMap(){
    pill(9,36,139,131,8,shade(7,11,22));rect(13,40,131,123,shade(5,10,17));
    for(int k=-3;k<=3;++k){
        int p=mapX(k*8.0f);rect(p-3,40,6,123,shade(8,19,26));
        p=mapY(k*8.0f);rect(13,p-3,131,6,shade(8,19,26));
    }
    for(int k=-3;k<=2;++k)for(int l=-3;l<=2;++l){
        int x=mapX(k*8.0f+4),y=mapY(l*8.0f+4);
        rect(x-6,y-6,12,12,(hashCell(k,l)%4==1)?shade(18,10,22):shade(11,17,24));
    }
    mapDot(0,0,4,shade(7,28,14));
    for(int i=0;i<8;++i)if(traffic[i].police&&heat>.8f)mapDot(traffic[i].x,traffic[i].z,2,shade(30,6,10));
    mapDot(target.x,target.z,4,carrying?shade(6,29,31):shade(31,21,5));
    if(customWaypoint)mapDot(personal.x,personal.z,3,shade(27,11,30));
    int x=mapX(px),y=mapY(pz);
    circle(x,y,4,shade(2,6,12));circle(x,y,3,shade(7,30,31));
    line(x,y,x+(int)(sinf(heading)*8),y-(int)(cosf(heading)*8),shade(31,31,31));
}
static void meter(int x,int y,int width,float fraction,u16 fill){
    pill(x,y,width,7,2,shade(7,10,16));
    int amount=(int)((width-4)*clampf(fraction,0,1));if(amount>0)rect(x+2,y+2,amount,3,fill);
}
static void drawPlayHud(){
    drawMap();pill(152,36,95,131,8,shade(8,13,25));
    text(160,44,carrying?"DELIVER":"PICKUP",carrying?shade(7,29,31):shade(31,22,8),1,10);
    char s[32];snprintf(s,sizeof s,"%02d:%02d",missionFrames/3600,(missionFrames/60)%60);
    text(160,58,s,shade(31,31,31),1,10);
    rect(159,73,80,1,shade(12,20,26));
    snprintf(s,sizeof s,"%03d KM/H",(int)(fabsf(speed)*510));text(160,81,s,shade(25,30,31),1,11);
    text(160,101,"CAR",shade(18,21,25));
    text(192,101,carClass==0?"STREET":carClass==1?"TURBO":"TANK",shade(7,29,31),1,6);
    text(160,115,"HEAT",shade(18,21,25));
    for(int i=0;i<5;++i)rect(160+i*16,126,12,5,i<(int)ceilf(heat)?shade(30,6,11):shade(7,11,17));
    pill(157,141,84,21,5,shade(6,18,26));text(165,147,"L BOOST",shade(8,30,31));
    static const char*messages[]={"PACKAGE SECURED!","DELIVERY COMPLETE!","BUMP! KEEP MOVING","NITRO BOOST!","THE NIGHT IS YOURS"};
    if(boxedFrames>0&&mode==PLAY){
        snprintf(s,sizeof s,"BOXED IN! ESCAPE %d",(90-boxedFrames+59)/60);
        text(11,174,s,shade(31,9,13),1,29);
    }else text(11,174,eventFlash>0?messages[eventKind]:"D-PAD DRIVE  A GAS B BRAKE",eventFlash>0?shade(8,29,31):shade(20,25,29),1,29);
    meter(158,136,81,nitro/100,shade(5,29,30));
    snprintf(s,sizeof s,"$%04d",cash);text(151,21,s,shade(31,24,9));
    meter(12,24,120,health/100,shade(8,30,17));
}
static void drawTitleHud(){
    pill(14,39,228,134,11,shade(8,13,26));pill(18,43,220,126,9,shade(11,18,30));
    rect(29,54,49,3,shade(6,29,31));
    text(29,65,"MIDNIGHT",shade(31,31,31),2,8);
    text(29,90,"CIRCUIT",shade(8,29,31),2,7);
    text(30,119,"A CITY AFTER DARK",shade(20,24,29));
    text(30,138,"HOLD MAP OR USE D-PAD",shade(20,24,29));
    pill(31,151,194,15,5,shade(7,22,29));text(56,154,"A OR UP TO DRIVE",shade(31,31,31));
    text(14,179,"ORIGINAL DS ARCADE GAME",shade(14,20,26));
}
static void overlay(const char*title,const char*subtitle){
    pill(14,49,228,115,12,shade(8,14,28));pill(18,53,220,107,9,shade(13,20,30));
    text(32,69,title,shade(31,31,31),2,11);
    text(32,111,subtitle,shade(8,29,31),1,24);
    pill(31,137,193,17,5,shade(6,21,29));
    text(48,142,mode==OVER?"TOUCH TO TRY AGAIN":"TOUCH TO RESUME",shade(31,31,31));
}
static void renderHud(){
    for(int y=0;y<SH;++y)rect(0,y,SW,1,shade(2+y/90,4+y/90,10+y/95));
    circle(243,1,53,shade(5,10,23));circle(224,4,31,shade(6,14,30));
    text(13,8,"MIDNIGHT CIRCUIT",shade(31,31,31),1,17);
    text(171,8,soundOn?"MUSIC ON":"MUSIC OFF",shade(8,29,31),1,9);
    rect(12,21,43,2,shade(7,29,31));
    if(mode==TITLE)drawTitleHud();
    else{
        drawPlayHud();
        if(mode==PAUSE)overlay("PAUSED","THE CITY CAN WAIT");
        if(mode==OVER){
            if(endReason==BUSTED)overlay("BUSTED","POLICE BOXED YOU IN");
            else if(endReason==OUT_OF_TIME)overlay("TIME UP","THE DELIVERY EXPIRED");
            else overlay("RUN OVER","THE NIGHT WINS THIS ROUND");
        }
    }
    rect(0,190,SW,2,shade(7,29,31));
    dmaCopy(hud,hudVram,sizeof hud);
}

int main(){
    lcdMainOnTop();videoSetMode(MODE_0_3D);videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    int sub=bgInitSub(3,BgType_Bmp16,BgSize_B16_256x256,0,0);
    hudVram=(u16*)bgGetGfxPtr(sub);bgUpdate();
    glInit();glEnable(GL_ANTIALIAS);glEnable(GL_BLEND);
    glClearColor(2,3,8,31);glClearPolyID(63);glClearDepth(GL_MAX_DEPTH);
    glViewport(0,0,255,191);
    soundEnable();
    DC_FlushRange((void*)song_bin,song_bin_size);
    initTraffic();
    while(pmMainLoop()){
        swiWaitForVBlank();scanKeys();int held=keysHeld(),pressed=keysDown();
        touchPosition touch={};if(held&KEY_TOUCH)touchRead(&touch);
        if(mode==TITLE){if(pressed&(KEY_A|KEY_UP|KEY_START|KEY_TOUCH))resetGame();}
        else if(mode==OVER){if(pressed&(KEY_A|KEY_UP|KEY_START|KEY_TOUCH))resetGame();}
        else if(mode==PAUSE){if(pressed&(KEY_A|KEY_UP|KEY_START|KEY_TOUCH))mode=PLAY;}
        else if(pressed&KEY_START){mode=PAUSE;}
        else updateGame(held,pressed,touch);
        renderWorld();renderHud();updateAudio();if(eventFlash>0)--eventFlash;++frame;
    }
    soundKill(musicSound);return 0;
}
