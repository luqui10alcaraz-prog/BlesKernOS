#include "plotters.h"
static bk_gui_rect_t clip_rect(bk_gui_rect_t a, bk_gui_rect_t b) {
    int x0=a.x>b.x?a.x:b.x, y0=a.y>b.y?a.y:b.y;
    int x1=a.x+a.w<b.x+b.w?a.x+a.w:b.x+b.w;
    int y1=a.y+a.h<b.y+b.h?a.y+a.h:b.y+b.h;
    return (bk_gui_rect_t){x0,y0,x1>x0?x1-x0:0,y1>y0?y1-y0:0};
}
void nsbk_plotter_init(nsbk_plotter_t *p,bk_gui_surface_t*s,bk_gui_rect_t c){if(p){p->surface=s;p->clip=c;}}
void nsbk_plot_fill(nsbk_plotter_t*p,bk_gui_rect_t r,uint32_t c){if(!p||!p->surface)return;r=clip_rect(r,p->clip);if(r.w>0&&r.h>0)bk_gui_surface_fill_rect(p->surface,r,c);}
void nsbk_plot_rect(nsbk_plotter_t*p,bk_gui_rect_t r,uint32_t c){if(p&&p->surface)bk_gui_surface_draw_rect(p->surface,r,c);}
void nsbk_plot_line(nsbk_plotter_t*p,int x0,int y0,int x1,int y1,uint32_t c){if(p&&p->surface)bk_gui_surface_draw_line(p->surface,x0,y0,x1,y1,c);}
void nsbk_plot_text(nsbk_plotter_t*p,int x,int y,const char*t,uint32_t n,uint32_t c,uint8_t px,bool b,bool i,bool m){if(p&&p->surface&&t)bk_gui_surface_draw_text_px(p->surface,x,y,t,n,c,px?px:12,b,i,m,p->clip);}
void nsbk_plot_bitmap(nsbk_plotter_t*p,bk_gui_rect_t r,const bk_gui_image_t*i){if(p&&p->surface&&i)bk_gui_surface_draw_image(p->surface,r,p->clip,i);}
