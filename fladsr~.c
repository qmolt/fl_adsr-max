#include "fladsr~.h"

void ext_main(void *r)
{
	t_class *c = class_new("fladsr~", (method)fl_adsr_new, (method)fl_adsr_free, sizeof(t_fl_adsr), 0, A_GIMME, 0);

	class_addmethod(c, (method)fl_adsr_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)fl_adsr_assist, "assist", A_CANT, 0);

	class_addmethod(c, (method)fl_adsr_float, "float", A_FLOAT, 0);
	class_addmethod(c, (method)fl_adsr_int, "int", A_LONG, 0);
	class_addmethod(c, (method)fl_adsr_bang, "bang", 0);

	class_addmethod(c, (method)fl_adsr_dsr_list, "dsr", A_GIMME, 0);
	class_addmethod(c, (method)fl_adsr_attack, "attack", A_GIMME, 0);
	class_addmethod(c, (method)fl_adsr_curvemode, "mode", A_GIMME, 0);

	class_dspinit(c);

	class_register(CLASS_BOX, fl_adsr_class);
	fl_adsr_class = c;
}

void *fl_adsr_new(t_symbol *s, short argc, t_atom *argv)
{
	t_fl_adsr *x = (t_fl_adsr *)object_alloc(fl_adsr_class);

	dsp_setup((t_pxobject *)x, 0);
	x->m_outlet = outlet_new((t_object *)x, "signal");
	x->obj.z_misc |= Z_NO_INPLACE;

	x->play_task = PT_NONE;

	x->samp_count_a = 0;
	x->ms_attack = DFLT_MS_ATTACK;
	x->curve_attack = DFLT_PROCCURVE;
	x->samp_count_dsr = 0;
	x->ms_dsr = DFLT_MS_DSR;

	x->curve_mode = CM_LINEAR;

	x->fs = sys_getsr();

	x->len_pts = DFLT_BRKPTS;
	x->pts_curve = (fl_pt *)sysmem_newptr(MAX_BRKPTS * sizeof(fl_pt));
	if (x->pts_curve == NULL) { object_error((t_object *)x, "out of memory: no space for curve"); return x; }
	else {
		for (long i = 0; i < MAX_BRKPTS; i++) {
			x->pts_curve[i].y = 0.0;
			x->pts_curve[i].dt = 0.0;
			x->pts_curve[i].c = 0.5;
		}
	}

	x->table_size = DFLT_TABLE_SIZE;
	x->wavetable_new = (float *)sysmem_newptr(DFLT_TABLE_SIZE * sizeof(float));
	if (x->wavetable_new == NULL) { object_error((t_object *)x, "out of memory: no space for wavetable"); return x; }
	else {
		for (int i = 0; i < DFLT_TABLE_SIZE; i++) {
			x->wavetable_new[i] = (DFLT_TABLE_SIZE - 1 - i) / (float)(DFLT_TABLE_SIZE - 1);
		}
	}
	x->wavetable = (float *)sysmem_newptr(DFLT_TABLE_SIZE * sizeof(float));
	if (x->wavetable == NULL) { object_error((t_object *)x, "out of memory: no space for wavetable"); return x; }
	else{
		for (int i = 0; i < DFLT_TABLE_SIZE; i++) {
			x->wavetable[i] = (DFLT_TABLE_SIZE - 1 - i) / (float)(DFLT_TABLE_SIZE - 1);
		}
	}

	x->dirty = 0;

	return x;
}

void fl_adsr_int(t_fl_adsr *x, long n) 
{	
	if (n != n) { return; }
	
	long ms_envelope = MAX(MIN_MS_ENV, n);
	long ms_attack = MIN(x->ms_attack, ms_envelope);
	x->ms_dsr = MAX(MIN_MS_ENV, (ms_envelope - ms_attack));

	fl_adsr_bang(x);
}

void fl_adsr_float(t_fl_adsr *x, double f)
{
	fl_adsr_int(x, (long)f);
}

void fl_adsr_bang(t_fl_adsr *x) 
{
	x->play_task = PT_NONE;

	if (x->dirty) { fl_adsr_update_wavetable(x); }
	x->play_task = PT_ATTACK;
	x->samp_count_a = 0;
	x->samp_count_dsr = 0;

	x->play_task = PT_ATTACK;
}

void fl_adsr_attack(t_fl_adsr *x, t_symbol *s, short argc, t_atom *argv)
{
	long ac = argc;
	t_atom *ap = argv;
	long attack;
	float curve = DFLT_PROCCURVE;

	if (ac < 1 && ac > 3) { object_error((t_object *)x,"attack: [1-2args] attack time, (curve)"); return; }
	
	if (atom_gettype(ap) != A_FLOAT && atom_gettype(ap) != A_LONG) { object_error((t_object *)x, "attack: argument must be a number"); return; }
	attack = (long)atom_getlong(ap);

	if(ac == 2){
		if (atom_gettype(ap + 1) != A_FLOAT && atom_gettype(ap + 1) != A_LONG) { object_error((t_object *)x, "attack: argument must be a number"); return; }
		curve = (float)atom_getfloat(ap + 1);
	}

	x->ms_attack = (long)MAX(MIN_MS_ENV, attack);
	x->curve_attack = signorm2pow(curve);
}

void fl_adsr_dsr_list(t_fl_adsr *x, t_symbol *s, short argc, t_atom *argv)
{
	long ac = argc;
	t_atom *ap = argv;

	fl_pt *pts_c = x->pts_curve;

	short curve_mode = x->curve_mode;
	short brkpt_size = curve_mode ? 3 : 2; //mode 0:linear (f,f) //mode 1:curve (f,f,f)
	float value;
	float domain;
	//float range_i, range_f;
	long n_brkpt = 0;
	long j, k;

	long table_size = x->table_size;
	float *wavetable_new = x->wavetable_new;
	float x_i, y_i, y_f;
	long segment;
	float curve;

	if (ac < 2) { return; }

	n_brkpt = ac / brkpt_size;
	if (n_brkpt > MAX_BRKPTS) { object_error((t_object *)x, "dsr: too many points"); return; }//debug

	domain = 0.;
	//range_i = range_f = (float)atom_getfloat(ap + 1);
	for (long i = 0; i < ac; i++) {
		
		value = (float)atom_getfloat(ap + i);

		j = i % brkpt_size;	//line-curve format (y,dx)(y,dx,c)
		k = i / brkpt_size;
		pts_c[k].c = DFLT_PROCCURVE;

		if (j == 0) {
			//range_i = MIN(range_i, value);
			//range_f = MAX(range_f, value);

			pts_c[k].y = value;
		}
		else if (j == 1) {
			domain += value;

			pts_c[k].dt = value;
		}
		else {
			pts_c[k].c = signorm2pow(value);
		}
	}
	x->len_pts = n_brkpt;

	k = 0;
	j = 0;
	x_i = 0.0;
	y_i = 1.0;
	y_f = pts_c[0].y;
	segment = (long)(pts_c[0].dt / domain * (float)table_size);
	curve = pts_c[0].c;

	for (long i = 0; i < table_size; i++) {	//k:puntos i:samps_table j:samps_segm 
		if (j > segment) {
			j = 0;

			if (k < n_brkpt) { k++; }

			y_i = y_f;
			segment = (long)(pts_c[k].dt / domain * (float)table_size);
			y_f = pts_c[k].y;
			curve = pts_c[k].c;
		}

		x_i = (j / (float)MAX(segment, 1));
		wavetable_new[i] = ((float)pow(x_i, curve)) * (y_f - y_i) + y_i;

		j++;
	}

	x->dirty = 1;
	if (x->play_task == PT_NONE) { fl_adsr_update_wavetable(x); }
}

void fl_adsr_update_wavetable(t_fl_adsr *x) 
{
	long table_size = x->table_size;
	float *wavetable_new = x->wavetable_new;
	float *wavetable = x->wavetable;
	
	for (long i = 0; i < table_size; i++) {
		wavetable[i] = wavetable_new[i];
	}

	x->dirty = 0;
}

void fl_adsr_curvemode(t_fl_adsr *x, t_symbol *s, short argc, t_atom *argv)
{
	long ac = argc;
	t_atom *ap = argv;
	short mode;

	if (ac != 1) { object_error((t_object *)x, "mode: linear 1/ curve 0"); return; }

	if (atom_gettype(ap) != A_LONG && atom_gettype(ap) != A_FLOAT) { object_error((t_object *)x, "mode: argument must be a number (1/0)"); return; }
	
	mode = (short)atom_getlong(ap);

	x->curve_mode = mode ? CM_CURVE : CM_LINEAR;
}

void fl_adsr_assist(t_fl_adsr *x, void *b, long msg, long arg, char *dst)
{
	if (msg == ASSIST_INLET) {
		switch (arg) {
		case I_INPUT: sprintf(dst, "(bang/float) start envelope");
			break;
		}
	}
	else if (msg == ASSIST_OUTLET) {
		switch (arg) {
		case O_OUTPUT: sprintf(dst, "(sig~) Envelope");
			break;
		}
	}
}

void fl_adsr_free(t_fl_adsr *x)
{
	dsp_free((t_pxobject *)x);

	sysmem_freeptr(x->pts_curve);
	sysmem_freeptr(x->wavetable_new);
	sysmem_freeptr(x->wavetable);
}

void fl_adsr_dsp64(t_fl_adsr *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	if (x->fs != samplerate) {
		x->fs = samplerate;

		//x->increment = (float)(x->table_size / x->fs);
	}

	object_method(dsp64, gensym("dsp_add64"), x, fl_adsr_perform64, 0, NULL);	/* attach the object to the DSP chain */
}

void fl_adsr_perform64(t_fl_adsr *x, t_object *dsp64, double **inputs, long numinputs, double **outputs, long numoutputs, long vectorsize, long flags, void *userparams)
{
	long n = vectorsize;

	t_double *out_env = outputs[0];

	float fs = (float)x->fs;

	long samp_count_a = x->samp_count_a;
	long samp_attack = (long)(x->ms_attack * fs * 0.001);
	float curve_attack = x->curve_attack;
	long samp_dsr = (long)(x->ms_dsr * fs * 0.001);
	long samp_count_dsr = x->samp_count_dsr;

	float *wavetable = x->wavetable;
	long max_index = x->table_size - 1;
	
	double env_value, val1, val2;;
	float att_norm;
	float dsr_norm;
	double findex, interp;
	long index;

	while (n--) {

		env_value = 0.0;

		if (x->play_task == PT_ATTACK) {
			if (samp_count_a >= samp_attack) {
				x->play_task = PT_DSR;
				env_value = 1.0;
			}
			else {
				att_norm = (float)samp_count_a / (float)samp_attack;
				env_value = (float)pow(att_norm, curve_attack);
			}
			
			samp_count_a++;
		}
		else if (x->play_task == PT_DSR) {
			if (samp_count_dsr > samp_dsr) {
				x->play_task = PT_NONE;
				env_value = 0.0;
			}
			else {
				dsr_norm = samp_count_dsr / (float)samp_dsr;
				findex = (double)max_index * dsr_norm;
				index = (long)floor(findex);
				interp = findex - index;

				val1 = wavetable[index];
				index = MIN(index + 1, max_index);
				val2 = wavetable[index];
				env_value = val1 + interp * (val2 - val1); /*v2 * interp + v1 * (1-interp)*/
			}
			
			samp_count_dsr++;
		}
		
		*out_env++ = env_value;
	}

	x->samp_count_a = samp_count_a;
	x->samp_count_dsr = samp_count_dsr;
}

float signorm2pow(float signorm_curve) {

	float value = (float)MIN(INCURVE_MAX, MAX(INCURVE_MIN, signorm_curve));
	if (value > 0.0f) {
		return 1.0f / (1.0f - value);
	}
	else {
		return value + 1.0f;
	}
}