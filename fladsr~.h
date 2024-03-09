#ifndef fl_adsr_h
#define fl_adsr_h

#include "ext.h"
#include "z_dsp.h"
#include "ext_obex.h"
#include <math.h>

#define DFLT_TABLE_SIZE 512
#define DFLT_ATTBRKPTS 2
#define DFLT_RELBRKPTS 1

#define DFLT_BRKPTS 0
#define MAX_BRKPTS 128

#define INCURVE_MIN -0.98
#define INCURVE_MAX 0.98

#define MIN_MS_ENVELOPE 10

enum CURVE_MODE { CM_LINEAR, CM_CURVE };
enum PLAY_TASK { PT_NONE, PT_ATTACK, PT_MIDDLE, PT_RELEASE };

typedef struct _fl_seg {
	long dt;	//delta (ms)
	float yi;	//start
	float yf;	//end
	float c;	//curve
	float r;	//ratio to parent
} fl_seg;

typedef struct _fl_adsr {
	t_pxobject obj;

	short play_task;

	long samp_count_att;
	long samp_count_mid;
	long samp_count_rel;

	long ms_attack;
	long samp_attack;
	long ms_middle;
	long samp_middle;
	long ms_release;
	long samp_release;

	short curve_mode;
	//long att_brkpts;
	//long rel_brkpts;

	fl_seg *segs;
	long len_segs;

	long table_size;
	//long table_att_start;
	long table_att_size;
	long table_att_end;
	long table_mid_start;
	long table_mid_size;
	long table_mid_end;
	long table_rel_start;
	long table_rel_size;
	long table_rel_end;
	
	float *wavetable_new;
	float *wavetable;

	short table_init;

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
void fl_adsr_list(t_fl_adsr *x, t_symbol *s, long argc, t_atom *argv);
void fl_adsr_update_wavetable(t_fl_adsr *x);
void fl_adsr_curvemode(t_fl_adsr *x, t_symbol *s, short argc, t_atom *argv);
void fl_adsr_assist(t_fl_adsr *x, void *b, long msg, long arg, char *dst);

void fl_adsr_free(t_fl_adsr *x);

void fl_adsr_dsp64(t_fl_adsr *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void fl_adsr_perform64(t_fl_adsr *x, t_object *dsp64, double **inputs, long numinputs, double **outputs, long numoutputs, long vectorsize, long flags, void *userparams);

float signorm2pow(float signorm_curve);

#endif