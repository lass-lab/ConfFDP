#include <stdint.h>
            struct a {
            uint32_t af    : 4; ///< Access Frequency
			uint32_t al    : 2; ///< Access Latency
			uint32_t sr    : 1; ///< Sequential Request
			uint32_t incom : 1; ///< Incompressible
			uint32_t rsvd3 : 8;
            union b
            {
                /* data */
                uint32_t dspec : 16; ///< Directive Specific
                // NvmeFDPDspec fdp_dspec;
                struct{
                    uint32_t rg : 8;
                    uint32_t ph : 8;
                };
            };
            
			
            };

typedef struct NvmeCmdDWORD13{
    union{	    
            
            struct a a;
            uint32_t val : 32;
    };
} NvmeCmdDWORD13;


typedef union NvmeCmdDWORD14{
    // union{	    
            
            struct{
            uint32_t af    : 4; ///< Access Frequency
			uint32_t al    : 2; ///< Access Latency
			uint32_t sr    : 1; ///< Sequential Request
			uint32_t incom : 1; ///< Incompressible
			uint32_t rsvd3 : 8;
            union 
            {
                /* data */
                uint32_t dspec : 16; ///< Directive Specific
                // NvmeFDPDspec fdp_dspec;
                struct{
                    uint32_t rg : 8;
                    uint32_t ph : 8;
                };
            };
            
			
            };
            uint32_t val;
    // };
} NvmeCmdDWORD14;
#include <stdio.h>

int main(){
    NvmeCmdDWORD13 d;
    printf("%lu %lu %lu\n",sizeof(NvmeCmdDWORD13),sizeof(struct a),sizeof(NvmeCmdDWORD14));
}