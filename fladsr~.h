#ifndef fl_adsr_h
#define fl_adsr_h

#include "ext.h"
#include "z_dsp.h"
#include "ext_obex.h"
#include <math.h>

#define DFLT_TABLE_SIZE 512

#define DFLT_BRKPTS 0
#define MAX_BRKPTS 128

#define DFLT_PROCCURVE 1
#define INCURVE_MIN -0.98
#define INCURVE_MAX 0.98

enum CURVE_MODE { CM_LINEAR, CM_CURVE };
enum PLAY_TASK { PT_NONE, PT_ATTACK, PT_DSR };

#define MIN_MS_ENV 1
#define DFLT_MS_ATTACK 100
#define DFLT_MS_DSR 400

typedef struct _fl_pt {
	float dt;
	float y;
	float c;
} fl_pt;

typedef struct _fl_adsr {
	t_pxobject obj;

	short play_task;

	long samp_count_a;
	long ms_attack;
	float curve_attack;
	long samp_count_dsr;
	long ms_dsr;

	short curve_mode;

	fl_pt *pts_curve;
	long len_pts;

	long table_size;
	
	float *wavetable_new;
	float *wavetable;

	double fs;

	short dirty;

	void *m_outlet;

} t_fl_adsr;

enum INLETS { I_INPUT, NUM_INLETS };
enum OUTLETS { O_OUTPUT, NUM_OUTLETS };

static t_class *fl_adsr_class;

void *fl_adsr_new(t_symbol *s, short argc, t_atom *argv);
void fl_adsr_int(t_fl_adsr *x, long n);
void fl_adsr_float(t_fl_adsr *x, double f);
void fl_adsr_bang(t_fl_adsr *x);
void fl_adsr_attack(t_fl_adsr *x, t_symbol *msg, short argc, t_atom *argv);
void fl_adsr_dsr_list(t_fl_adsr *x, t_symbol *msg, short argc, t_atom *argv);
void fl_adsr_update_wavetable(t_fl_adsr *x);
void fl_adsr_curvemode(t_fl_adsr *x, t_symbol *s, short argc, t_atom *argv);
void fl_adsr_assist(t_fl_adsr *x, void *b, long msg, long arg, char *dst);

void fl_adsr_free(t_fl_adsr *x);

void fl_adsr_dsp64(t_fl_adsr *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void fl_adsr_perform64(t_fl_adsr *x, t_object *dsp64, double **inputs, long numinputs, double **outputs, long numoutputs, long vectorsize, long flags, void *userparams);

float signorm2pow(float signorm_curve);

#endif