#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <util.h>

#include <anetlist/entities.h>
#include <anetlist/net.h>
#include <anetlist/primitives.h>
#include <anetlist/bels.h>

static int is_external(struct anetlist_instance **instances, int n_instances, struct anetlist_instance *inst)
{
	int i;
	
	for(i=0;i<n_instances;i++)
		if(instances[i] == inst)
			return 0;
	return 1;
}

static int lookup_pin_to(struct anetlist_endpoint **eps, int n_eps, struct anetlist_instance *inst, int pin)
{
	int i;
	struct anetlist_endpoint *ep;
	
	//printf("looking for pin %d of %p (%s)\n", pin, inst, inst->e->name);
	for(i=0;i<n_eps;i++) {
		ep = eps[i];
		while(ep != NULL) {
			//printf("hit pin %d of %p (%s)\n", ep->pin, ep->inst, ep->inst->e->name);
			if((ep->inst == inst) && (ep->pin == pin))
				return i;
			ep = ep->next;
		}
	}
	return -1;
}

static void replace(struct anetlist *a, struct anetlist_instance **instances, int n_instances, struct anetlist_entity *te, char **tattributes, struct anetlist_endpoint **tinputs, struct anetlist_endpoint **toutputs)
{
	int i, j;
	struct anetlist_endpoint *ep, *extep;
	
	/* update cross references */
	for(i=0;i<n_instances;i++) {
		/* outputs of the replaced block: update external inputs they are connected to */
		for(j=0;j<instances[i]->e->n_outputs;j++) {
			ep = instances[i]->outputs[j];
			while(ep != NULL) {
				if(is_external(instances, n_instances, ep->inst)) {
					extep = ep->inst->inputs[ep->pin];
					assert((extep != NULL) && (extep->next == NULL));
					extep->inst = instances[0];
					extep->pin = lookup_pin_to(toutputs, te->n_outputs, ep->inst, ep->pin);
					assert(extep->pin != -1);
				}
				ep = ep->next;
			}
		}
		
		/* inputs of the replaced block: update external outputs they are connected to */
		for(j=0;j<instances[i]->e->n_inputs;j++) {
			ep = instances[i]->inputs[j];
			if(ep != NULL) {
				if(is_external(instances, n_instances, ep->inst)) {
					extep = ep->inst->outputs[ep->pin];
					while((extep->inst != instances[i]) || (extep->pin != j))
						extep = extep->next;
					extep->inst = instances[0];
					extep->pin = lookup_pin_to(tinputs, te->n_inputs, ep->inst, ep->pin);
					assert(extep->pin != -1);
				}
			}
		}
	}
	
	/* remove absorbed instances */
	for(i=1;i<n_instances;i++)
		anetlist_remove_instance(a, instances[i]);
	
	/* do the replacement proper */
	for(i=0;i<instances[0]->e->n_attributes;i++)
		free(instances[0]->attributes[i]);
	free(instances[0]->attributes);
	instances[0]->attributes = tattributes;
	anetlist_free_endpoint_array(instances[0]->inputs, instances[0]->e->n_inputs);
	instances[0]->inputs = tinputs;
	anetlist_free_endpoint_array(instances[0]->outputs, instances[0]->e->n_outputs);
	instances[0]->outputs = toutputs;
	instances[0]->e = te;
}

static int get_primitive_index(struct anetlist_entity *e)
{
	int len;
	int index;
	
	len = sizeof(anetlist_primitives)/sizeof(anetlist_primitives[0]);
	index = e - anetlist_primitives;
	if(index >= len)
		index = -1;
	return index;
}

static struct anetlist_endpoint *endpoints_dup(struct anetlist_endpoint *ep)
{
	struct anetlist_endpoint *r;
	struct anetlist_endpoint *n;
	
	r = NULL;
	while(ep != NULL) {
		n = alloc_type(struct anetlist_endpoint);
		n->inst = ep->inst;
		n->pin = ep->pin;
		n->next = r;
		r = n;
		ep = ep->next;
	}
	
	return r;
}

static void transform_ibuf(struct anetlist *a, struct anetlist_instance *ibuf)
{
	struct anetlist_entity *te;
	char **tattributes;
	struct anetlist_endpoint **tinputs;
	struct anetlist_endpoint **toutputs;
	struct anetlist_instance *instances[2];
	char *name;
	
	te = &anetlist_bels[ANETLIST_BEL_IOBM];
	anetlist_init_instance_fields(te, &tattributes, &tinputs, &toutputs);
	// TODO: how to set the IOB in pure input mode?
	toutputs[ANETLIST_BEL_IOBM_I] = endpoints_dup(ibuf->outputs[ANETLIST_PRIMITIVE_IBUF_O]);
	instances[0] = ibuf;
	instances[1] = ibuf->inputs[ANETLIST_PRIMITIVE_IBUF_I]->inst; /* remove the input port */
	name = stralloc(instances[1]->uid); /* save input port name (needed to identify the I/O) */
	replace(a, instances, 2, te, tattributes, tinputs, toutputs);
	/* rename the IOB */
	free(instances[0]->uid);
	instances[0]->uid = name;
}

static void transform_obuf(struct anetlist *a, struct anetlist_instance *obuf)
{
	struct anetlist_entity *te;
	char **tattributes;
	struct anetlist_endpoint **tinputs;
	struct anetlist_endpoint **toutputs;
	struct anetlist_instance *instances[2];
	char *name;
	
	te = &anetlist_bels[ANETLIST_BEL_IOBM];
	anetlist_init_instance_fields(te, &tattributes, &tinputs, &toutputs);
	// TODO: how to set the IOB in pure output mode?
	tinputs[ANETLIST_BEL_IOBM_O] = endpoints_dup(obuf->inputs[ANETLIST_PRIMITIVE_OBUF_I]);
	instances[0] = obuf;
	instances[1] = obuf->outputs[ANETLIST_PRIMITIVE_OBUF_O]->inst; /* remove the output port */
	name = stralloc(instances[1]->uid); /* save output port name (needed to identify the I/O) */
	replace(a, instances, 2, te, tattributes, tinputs, toutputs);
	/* rename the IOB */
	free(instances[0]->uid);
	instances[0]->uid = name;
}

static void transform_lut(struct anetlist *a, struct anetlist_instance *lut)
{
	struct anetlist_entity *te;
	char **tattributes;
	struct anetlist_endpoint **tinputs;
	struct anetlist_endpoint **toutputs;
	int i;
	
	te = &anetlist_bels[ANETLIST_BEL_LUT6_2];
	anetlist_init_instance_fields(te, &tattributes, &tinputs, &toutputs);
	strcpy(tattributes[0]+16-strlen(lut->attributes[0]), lut->attributes[0]);
	for(i=0;i<lut->e->n_inputs;i++)
		tinputs[i] = endpoints_dup(lut->inputs[i]);
	toutputs[ANETLIST_BEL_LUT6_2_O6] = endpoints_dup(lut->outputs[0]);
	replace(a, &lut, 1, te, tattributes, tinputs, toutputs);
}

static void transform_bufgp(struct anetlist *a, struct anetlist_instance *bufgp)
{
	struct anetlist_entity *te;
	char **tattributes;
	struct anetlist_endpoint **tinputs;
	struct anetlist_endpoint **toutputs;
	struct anetlist_instance *instances[2];
	char *name;
	struct anetlist_instance *iob;
	
	/* promote the BUFG to BUFGMUX */
	te = &anetlist_bels[ANETLIST_BEL_BUFGMUX];
	anetlist_init_instance_fields(te, &tattributes, &tinputs, &toutputs);
	// TODO: how to set the BUFGMUX in BUFG mode?
	toutputs[ANETLIST_BEL_BUFGMUX_O] = endpoints_dup(bufgp->outputs[ANETLIST_PRIMITIVE_BUFGP_O]);
	instances[0] = bufgp;
	instances[1] = bufgp->inputs[ANETLIST_PRIMITIVE_BUFGP_I]->inst; /* remove the input port */
	name = stralloc(instances[1]->uid); /* save input port name (needed to identify the I/O) */
	replace(a, instances, 2, te, tattributes, tinputs, toutputs);
	
	/* insert IOB at the input */
	iob = anetlist_instantiate(a, name, &anetlist_bels[ANETLIST_BEL_IOBM]);
	free(name);
	anetlist_connect(iob, ANETLIST_BEL_IOBM_I, instances[0], ANETLIST_BEL_BUFGMUX_I0);
}

static void transform_carrychain(struct anetlist *a, struct anetlist_instance *bufgp)
{
	printf("TODO: carry chain packing\n");
}

static void transform_fd(struct anetlist *a, struct anetlist_instance *fd)
{
	struct anetlist_entity *te;
	char **tattributes;
	struct anetlist_endpoint **tinputs;
	struct anetlist_endpoint **toutputs;
	
	te = &anetlist_bels[ANETLIST_BEL_FDRE];
	anetlist_init_instance_fields(te, &tattributes, &tinputs, &toutputs);
	tinputs[ANETLIST_BEL_FDRE_C] = endpoints_dup(fd->inputs[ANETLIST_PRIMITIVE_FD_C]);
	tinputs[ANETLIST_BEL_FDRE_D] = endpoints_dup(fd->inputs[ANETLIST_PRIMITIVE_FD_D]);
	toutputs[ANETLIST_BEL_FDRE_Q] = endpoints_dup(fd->outputs[ANETLIST_PRIMITIVE_FD_Q]);
	replace(a, &fd, 1, te, tattributes, tinputs, toutputs);
}

static void transform_fde(struct anetlist *a, struct anetlist_instance *fde)
{
	struct anetlist_entity *te;
	char **tattributes;
	struct anetlist_endpoint **tinputs;
	struct anetlist_endpoint **toutputs;
	
	te = &anetlist_bels[ANETLIST_BEL_FDRE];
	anetlist_init_instance_fields(te, &tattributes, &tinputs, &toutputs);
	tinputs[ANETLIST_BEL_FDRE_C] = endpoints_dup(fde->inputs[ANETLIST_PRIMITIVE_FDE_C]);
	tinputs[ANETLIST_BEL_FDRE_CE] = endpoints_dup(fde->inputs[ANETLIST_PRIMITIVE_FDE_CE]);
	tinputs[ANETLIST_BEL_FDRE_D] = endpoints_dup(fde->inputs[ANETLIST_PRIMITIVE_FDE_D]);
	toutputs[ANETLIST_BEL_FDRE_Q] = endpoints_dup(fde->outputs[ANETLIST_PRIMITIVE_FDE_Q]);
	replace(a, &fde, 1, te, tattributes, tinputs, toutputs);
}

void transform(struct anetlist *a)
{
	struct anetlist_instance *inst;
	int pi;
	
	inst = a->head;
	while(inst != NULL) {
		switch(inst->e->type) {
			case ANETLIST_ENTITY_INTERNAL:
				pi = get_primitive_index(inst->e);
				switch(pi) {
					case ANETLIST_PRIMITIVE_IBUF:
						transform_ibuf(a, inst);
						break;
					case ANETLIST_PRIMITIVE_OBUF:
						transform_obuf(a, inst);
						break;
					case ANETLIST_PRIMITIVE_LUT1:
					case ANETLIST_PRIMITIVE_LUT2:
					case ANETLIST_PRIMITIVE_LUT3:
					case ANETLIST_PRIMITIVE_LUT4:
					case ANETLIST_PRIMITIVE_LUT5:
					case ANETLIST_PRIMITIVE_LUT6:
						transform_lut(a, inst);
						break;
					case ANETLIST_PRIMITIVE_BUFGP:
						transform_bufgp(a, inst);
						break;
					case ANETLIST_PRIMITIVE_MUXCY:
					case ANETLIST_PRIMITIVE_XORCY:
						transform_carrychain(a, inst);
						break;
					case ANETLIST_PRIMITIVE_FD:
						transform_fd(a, inst);
						break;
					case ANETLIST_PRIMITIVE_FDE:
						transform_fde(a, inst);
						break;
					default:
						if(!inst->e->bel) {
							fprintf(stderr, "Unhandled primitive: %s\n", inst->e->name);
							exit(EXIT_FAILURE);
						}
						break;
				}
				break;
			case ANETLIST_ENTITY_PORT_OUT:
			case ANETLIST_ENTITY_PORT_IN:
				/* nothing to do: those get removed as part of I/O buffer transformation */
				break;
			default:
				assert(0);
				break;
		}
		inst = inst->next;
	}
}