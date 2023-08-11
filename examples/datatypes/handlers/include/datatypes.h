#ifndef __DATATYPES_H__
#define __DATATYPES_H__ 

typedef struct spin_core_state{
    MPIT_Segment state; //< TODO get this dependent on the number of cores;
    struct MPIT_m2m_params params;
    uint32_t msg_start;   
}__attribute__((packed, aligned(32))) spin_core_state_t;


typedef struct spin_datatype_mem{
    // this pointer is populated by the host
    DECL_PTR(spin_core_state_t *, state)
}__attribute__((packed, aligned(32))) spin_datatype_mem_t;

#endif /* __DATATYPES_H__ */
