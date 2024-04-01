#include "fladsr~.h"

void ext_main(void *r)
{
	t_class *c = class_new("fladsr~", (method)fl_adsr_new, (method)fl_adsr_free, sizeof(t_fl_adsr), 0, A_GIMME, 0);

	class_addmethod(c, (method)fl_adsr_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)fl_adsr_assist, "assist", A_CANT, 0);

	class_addmethod(c, (method)fl_adsr_float, "float", A_FLOAT, 0);
	class_addmethod(c, (method)fl_adsr_int, "int", A_LONG, 0);
	class_addmethod(c, (method)fl_adsr_bang, "bang", 0);
	class_addmethod(c, (method)fl_adsr_list, "list", A_GIMME, 0);
	class_addmethod(c, (method)fl_adsr_curvemode, "mode", A_GIMME, 0);

	class_dspinit(c);

	class_register(CLASS_BOX, fl_adsr_class);
	fl_adsr_class = c;
}

void *fl_adsr_new(t_symbol *s, short argc, t_atom *argv)
{
	t_fl_adsr *x = (t_fl_adsr *)object_alloc(fl_adsr_class);

	inlet_new((t_object *)x, "list");
	dsp_setup((t_pxobject *)x, 0);
	x->m_outlet = outlet_new((t_object *)x, "signal");
	x->obj.z_misc |= Z_NO_INPLACE;

	x->play_task = PT_NONE;

	x->samp_count_att = 0;
	x->samp_count_mid = 0;
	x->samp_count_rel = 0;

	x->ms_attack = 0;
	x->samp_attack = 0;
	x->ms_middle = 0;
	x->samp_middle = 0;
	x->ms_release = 0;
	x->samp_release = 0;

	x->curve_mode = CM_LINEAR;
	//x->att_brkpts = DFLT_ATTBRKPTS;
	//x->rel_brkpts = DFLT_RELBRKPTS;

	x->fs = sys_getsr();

	x->len_segs = DFLT_BRKPTS;
	x->segs = (fl_seg *)sysmem_newptr(MAX_BRKPTS * sizeof(fl_seg));
	if (x->segs == NULL) { object_error((t_object *)x, "out of memory: no space for curve"); return x; }
	else {
		for (long i = 0; i < MAX_BRKPTS; i++) {
			x->segs[i].dt = 0;
			x->segs[i].yi = 0.0;
			x->segs[i].yf = 0.0;
			x->segs[i].c = 1.0;
			x->segs[i].r = 1.0;
		}
	}

	x->table_size = DFLT_TABLE_SIZE;
	x->wavetable_new = (float *)sysmem_newptr(DFLT_TABLE_SIZE * sizeof(float));
	if (x->wavetable_new == NULL) { object_error((t_object *)x, "out of memory: no space for wavetable"); return x; }
	else {
		for (int i = 0; i < DFLT_TABLE_SIZE; i++) {
			x->wavetable_new[i] = 0.0;
		}
	}
	x->wavetable = (float *)sysmem_newptr(DFLT_TABLE_SIZE * sizeof(float));
	if (x->wavetable == NULL) { object_error((t_object *)x, "out of memory: no space for wavetable"); return x; }
	else{
		for (int i = 0; i < DFLT_TABLE_SIZE; i++) {
			x->wavetable[i] = 0.0;
		}
	}
	x->table_init = 0;

	x->table_att_size = x->table_size / 4;
	//x->table_att_start = 0;
	x->table_att_end = x->table_att_size - 1;

	x->table_mid_size = x->table_size / 2;
	x->table_mid_start = x->table_att_end + 1;
	x->table_mid_end = x->table_mid_start + x->table_mid_size - 1;
	
	x->table_rel_size = x->table_size / 4;
	x->table_rel_start = x->table_mid_end + 1;
	x->table_rel_end = x->table_size - 1;

	x->dirty = 0;

	return x;
}

void fl_adsr_int(t_fl_adsr *x, long n) 
{	
	if (n != n) { return; }
	if (n < MIN_MS_ENVELOPE) { return; }
	
	long ms_attack = x->ms_attack;
	long ms_release = x->ms_release;
	long ms_envelope = n;
	long ms_middle = MAX(0, (ms_envelope - ms_attack - ms_release));

	x->samp_middle = (long)(ms_middle * x->fs * 0.001);

	fl_adsr_bang(x);
}

void fl_adsr_float(t_fl_adsr *x, double f)
{
	fl_adsr_int(x, (long)f);
}

void fl_adsr_bang(t_fl_adsr *x) 
{
	if (!x->table_init) { return; }

	x->play_task = PT_ATTACK;
	x->samp_count_att = 0;
	x->samp_count_mid = 0;
	x->samp_count_rel = 0;
}

void fl_adsr_list(t_fl_adsr *x, t_symbol *s, long argc, t_atom *argv) {
	long ac = argc;
	t_atom *ap = argv;

	fl_seg *segs = x->segs;
	short curve_mode = x->curve_mode;
	short brkpt_size = curve_mode ? 3 : 2; //mode 0:linear (f,f) //mode 1:curve (f,f,f)
	long att_brkpts = DFLT_ATTBRKPTS;	//x->att_brkpts;
	long rel_brkpts = DFLT_RELBRKPTS;	//x->rel_brkpts;
	long total_brkpts = ac / brkpt_size;
	long min_arg;

	long j = 0;
	long k = 0;
	long u = 0;
	long ms_value = 0;
	long domain = 0;
	float curve = 0;
	double fs = x->fs;

	float r = 0.0;
	long ms_accum = 0;
	float x0 = 0.0;
	float y_i = 0.0;
	float y_f = 0.0;
	short new_brkpt = 0;
	long table_section = 0;
	long table_past = 0;
	long ms_section = 0;

	long ms_attack = 0;
	long ms_middle = 0;
	long ms_release = 0;
	long table_att_size = x->table_att_size;
	long table_att_end = x->table_att_end;
	long table_mid_start = x->table_mid_start;
	long table_mid_size = x->table_mid_size;
	long table_mid_end = x->table_mid_end;
	long table_rel_start = x->table_rel_start;
	long table_rel_size = x->table_rel_size;
	long table_rel_end = x->table_rel_end;

	long table_size = x->table_size;
	float *wavetable_new = x->wavetable_new;

	if (curve_mode == CM_LINEAR) {
		min_arg = 2 * (rel_brkpts + att_brkpts + curve_mode); //attack + release
		if (ac < min_arg) { object_error((t_object *)x, "list: [>= %d args] %d attack + release breakpoints in line format", min_arg, att_brkpts); return; }
		if (ac > 2 * MAX_BRKPTS) { object_error((t_object *)x, "list: [< %d args] breakpoints max", MAX_BRKPTS); return; }
	}
	else {
		min_arg = 3 * (rel_brkpts + att_brkpts);
		if (ac < min_arg) { object_error((t_object *)x, "list: [>= %d args] %d attack + release breakpoints in curve format", min_arg, att_brkpts); return; }
		if (ac > 3 * MAX_BRKPTS) { object_error((t_object *)x, "list: [< %d args] breakpoints max", MAX_BRKPTS); return; } 
	}
	
	for (long i = 0; i < total_brkpts; i++) { //line-curve format (y,dx)(y,dx,c)
		
		//get atom data
		j = i * brkpt_size;

		if (i == 0) { segs[0].yi = 0; }
		else{ segs[i].yi = (float)atom_getfloat(ap + j - brkpt_size); }
		segs[i].yf = (float)atom_getfloat(ap + j);
		
		ms_value = (long)MAX(1, atom_getlong(ap + j + 1));
		segs[i].dt = ms_value;

		curve = (float)atom_getfloat(ap + j + 2);
		segs[i].c = signorm2pow(curve);
		
		//attack, middle, release ms duration
		if (i < att_brkpts) { ms_attack += (long)ms_value; }
		if (i > (total_brkpts - 1) - rel_brkpts) { ms_release += (long)ms_value; }
		domain += (long)ms_value;
		
	}
	ms_attack = MAX(1, ms_attack);
	ms_middle = MAX(1, domain - ms_attack - ms_release);
	ms_release = MAX(1, ms_release);

	//get ratios of each segment
	for (long i = 0; i < total_brkpts; i++) {
		if (i < att_brkpts) { segs[i].r = segs[i].dt / (float)ms_attack; }
		else if (i > (total_brkpts - 1) - rel_brkpts) { segs[i].r = segs[i].dt / (float)ms_release; }
		else{ segs[i].r = segs[i].dt / (float)ms_middle; }
	}

	k = 0;
	long accum = 0;
	float xx = 0.0;
	for (long i = 0; i < table_size; i++) { 
		
		//correct k
		if (i == table_mid_start) { k = att_brkpts; accum = 0; }
		else if (i == table_rel_start) { k = total_brkpts - rel_brkpts; accum = 0; }

		//get data
		r = segs[k].r;
		y_i = segs[k].yi;
		y_f = segs[k].yf;

		if (i < table_mid_start) { x0 = accum++ / (table_att_size * r); }
		else if (i < table_rel_start) { x0 = accum++ / (table_mid_size * r); }
		else { x0 = accum++ / (table_rel_size * r); }

		//f(x)
		xx = MAX(0.f, MIN(1.f, x0));
		if (!curve_mode) { wavetable_new[i] = xx * (y_f - y_i) + y_i; }
		else { wavetable_new[i] = ((float)pow(xx, curve)) * (y_f - y_i) + y_i; }

		//next k
		if (x0 >= 1.0) { k = MIN(k++, total_brkpts - 1); accum = 0; }
	}

	x->len_segs = total_brkpts;
	x->ms_attack = ms_attack;
	x->ms_middle = ms_middle;
	x->ms_release = ms_release;
	x->samp_attack = (long)(ms_attack * fs * 0.001);
	x->samp_middle = (long)(ms_middle * fs * 0.001);
	x->samp_release = (long)(ms_release * fs * 0.001);

	fl_adsr_update_wavetable(x);

	x->table_init = 1;
}

void fl_adsr_update_wavetable(t_fl_adsr *x) 
{
	x->dirty = 1;

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
		case I_INPUT: sprintf(dst, "(bang/float) start envelope"); break;
		case I_LIST: sprintf(dst, "(list) envelope (line format)"); break;
		}
	}
	else if (msg == ASSIST_OUTLET) {
		switch (arg) {
		case O_OUTPUT: sprintf(dst, "(sig~) Envelope"); break;
		}
	}
}

void fl_adsr_free(t_fl_adsr *x)
{
	dsp_free((t_pxobject *)x);

	sysmem_freeptr(x->segs);
	sysmem_freeptr(x->wavetable_new);
	sysmem_freeptr(x->wavetable);
}

void fl_adsr_dsp64(t_fl_adsr *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	double old_fs = x->fs;
	double fs = samplerate;
	double fs_rat = fs / old_fs;

	if (x->fs != fs) {
		
		x->fs = fs;
		x->samp_count_att = (long)(x->samp_count_att * fs_rat);
		x->samp_count_mid = (long)(x->samp_count_mid * fs_rat);
		x->samp_count_rel = (long)(x->samp_count_rel * fs_rat);
	}

	object_method(dsp64, gensym("dsp_add64"), x, fl_adsr_perform64, 0, NULL);	/* attach the object to the DSP chain */
}

void fl_adsr_perform64(t_fl_adsr *x, t_object *dsp64, double **inputs, long numinputs, double **outputs, long numoutputs, long vectorsize, long flags, void *userparams)
{
	long n = vectorsize;

	t_double *out_env = outputs[0];

	float fs = (float)x->fs;

	long samp_count_att = x->samp_count_att;
	long samp_count_mid = x->samp_count_mid;
	long samp_count_rel = x->samp_count_rel;

	long samp_attack = x->samp_attack;
	long samp_middle = x->samp_middle;
	long samp_release = x->samp_release;

	float *wavetable;
	if(x->dirty){wavetable = x->wavetable_new;}
	else { wavetable = x->wavetable; }
	
	long table_att_size = x->table_att_size;
	long table_att_end = x->table_att_end;
	long table_mid_start = x->table_mid_start;
	long table_mid_size = x->table_mid_size;
	long table_mid_end = x->table_mid_end;
	long table_rel_start = x->table_rel_start;
	long table_rel_size = x->table_rel_size;
	long table_rel_end = x->table_rel_end;

	double env_value, val1, val2;
	double att_norm, mid_norm, rel_norm;
	double findex, interp;
	long index;

	while (n--) {

		env_value = 0.0;

		if (x->play_task == PT_ATTACK) {
			
			if (samp_count_att < samp_attack) {
				att_norm = (double)(samp_count_att++) / (double)samp_attack;
			}
			else {
				att_norm = 1.0;
				x->play_task = PT_MIDDLE;
			}
			findex = (double)(table_att_size * att_norm);
			index = (long)floor(findex);
			interp = findex - index;
			val1 = wavetable[index];
			index = MIN(index + 1, table_att_end);
			val2 = wavetable[index];
			env_value = val1 + interp * (val2 - val1); // v2 * interp + v1 * (1-interp)
		}
		else if (x->play_task == PT_MIDDLE) {

			if (samp_count_mid < samp_middle) {
				mid_norm = (double)(samp_count_mid++) / (double)samp_middle;
			}
			else {
				mid_norm = 1.0;
				x->play_task = PT_RELEASE;
			}
			findex = (double)(table_mid_start + table_mid_size * mid_norm);
			index = (long)floor(findex);
			interp = findex - index;
			val1 = wavetable[index];
			index = MIN(index + 1, table_mid_end);
			val2 = wavetable[index];
			env_value = val1 + interp * (val2 - val1);
		}
		else if (x->play_task == PT_RELEASE) {
			if (samp_count_rel < samp_release) {
				rel_norm = (double)(samp_count_rel++) / (double)samp_release;
			}
			else {
				rel_norm = 1.0;
				x->play_task = PT_NONE;
			}
			findex = (double)(table_rel_start + table_rel_size * rel_norm);
			index = (long)floor(findex);
			interp = findex - index;
			val1 = wavetable[index];
			index = MIN(index + 1, table_rel_end);
			val2 = wavetable[index];
			env_value = val1 + interp * (val2 - val1);
		}
		//else { env_value = 0.0; } //PT_NONE
		
		*out_env++ = env_value;
	}

	x->samp_count_att = samp_count_att;
	x->samp_count_mid = samp_count_mid;
	x->samp_count_rel = samp_count_rel;
}

float signorm2pow(float signorm_curve) { //(- 1, 1) -> (0, +inf)

	float value = (float)MIN(INCURVE_MAX, MAX(INCURVE_MIN, signorm_curve));
	if (value > 0.0f) {
		return 1.0f / (1.0f - value);
	}
	else {
		return value + 1.0f;
	}
}