#include "ps5_screen.c"
#include "ps5_agc_package.c"
#include "util/u_sample_positions.c"
#include "compiler/nir/nir_serialize.h"
#include "util/blob.h"
extern const size_t *psbc_abi(void);
/* Captures are NIR-only; fail if the driver unexpectedly requests TGSI. */
nir_shader *tgsi_to_nir(const void *tokens, struct pipe_screen *screen, bool allow_disk_cache) {
   (void)tokens; (void)screen; (void)allow_disk_cache; abort();
}
int main(int argc,char **argv) {
   assert(argc==2); psbc_init();
   if(!strcmp(argv[1],"--contract")) {
      nir_shader_compiler_options opts=*psbc_get_nir_options(PSBC_STAGE_FRAGMENT);
      assert(!opts.lower_to_scalar_filter && !opts.lower_mediump_io && !opts.lower_convert_alu_types && !opts.varying_estimate_instr_cost && !opts.max_offset_shift && !opts.cb_data);
      opts.varying_expression_max_cost=NULL;
      opts.io_options |= nir_io_has_intrinsics;
      FILE *f=fopen("ballot-contract.bin","wb"); assert(f);
      assert(fwrite(psbc_abi(),22*sizeof(size_t),1,f)==1);
      assert(fwrite(&opts,sizeof(opts),1,f)==1); fclose(f);
   } else {
      FILE *f=fopen(argv[1],"rb"); assert(f);
      assert(!fseek(f,0,SEEK_END)); long size=ftell(f); assert(size>0 && size<16*1024*1024);
      rewind(f); void *data=malloc(size); assert(data);
      assert(fread(data,size,1,f)==1); fclose(f);
      struct blob_reader reader; blob_reader_init(&reader,data,size);
      nir_shader *nir=nir_deserialize(NULL,psbc_get_nir_options(PSBC_STAGE_FRAGMENT),&reader);
      assert(nir && !reader.overrun && reader.current==reader.end);
      nir_validate_shader(nir,"captured fragment deserialized");
      struct pipe_shader_state templ={.type=PIPE_SHADER_IR_NIR,.ir.nir=nir};
      struct ps5_shader *shader=ps5_create_shader_state(NULL,&templ,PSBC_STAGE_FRAGMENT,2);
      assert(shader && shader->active && shader->active->output.machine_code_size);
      struct ps5_vertex_layout layout={0};
      struct ps5_fragment_exports exports={.color_mask=1,.formats=4};
      assert(ps5_select_shader_variant(shader,2,&layout,0,false,false,false,false,false,-1,&exports));
      printf("PASS captured fragment PS5 backend: %s bytes=%zu\n",argv[1],shader->active->output.machine_code_size);
      struct ps5_context context={0}; ps5_delete_shader_state(&context.base,shader); free(data);
   }
   psbc_shutdown();
}
