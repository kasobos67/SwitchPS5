#include "standalone.cpp"
#include "compiler/glsl/gl_nir.h"
#include "compiler/nir/nir_builder.h"
#include "state_tracker/st_context.h"
#include "state_tracker/st_nir.h"
#include "compiler/nir/nir_serialize.h"
#include "util/blob.h"
#include "varying.inc"
extern "C" const size_t *mesa_abi(void);
int main(int argc, char **argv) {
   if(argc != 3) return 2;
   gl_context ctx = {};
   const standalone_options settings = {.glsl_version=460};
   options=&settings;
   initialize_context(&ctx, API_OPENGL_COMPAT);
   ctx.Version=46;
   FILE *contract=fopen("ballot-contract.bin","rb"); assert(contract);
   size_t abi[22]; assert(fread(abi,sizeof(abi),1,contract)==1);
   assert(!memcmp(abi,mesa_abi(),sizeof(abi)));
   nir_shader_compiler_options backend_options;
   assert(fread(&backend_options,sizeof(backend_options),1,contract)==1);
   assert(fgetc(contract)==EOF); fclose(contract);
   backend_options.varying_expression_max_cost=ac_nir_varying_expression_max_cost;
   ctx.screen->nir_options[MESA_SHADER_FRAGMENT]=&backend_options;
   ctx.Extensions.ARB_shader_ballot = strcmp(argv[1], "--no-ballot") != 0;
   ctx.Extensions.ARB_shader_group_vote = true;
   ctx.Const.Program[MESA_SHADER_FRAGMENT].MaxUniformBlocks=14;
   ctx.Const.MaxCombinedUniformBlocks=84;
   ctx.Const.NativeIntegers=true;
   ctx.Const.PackedDriverUniformStorage=false;
   gl_shader_program *program=standalone_create_shader_program();
   program->SeparateShader=true;
   const char *source=load_text_file(program,argv[2]);
   if(!source) return 2;
   gl_shader *shader=standalone_add_shader_source(&ctx,program,GL_FRAGMENT_SHADER,source);
   _mesa_glsl_compile_shader(&ctx,shader,nullptr,false,false,true);
   if(!shader->CompileStatus) { fprintf(stderr,"COMPILE FAIL: %s\n",shader->InfoLog); return 1; }
   program->data->LinkStatus=LINKING_SUCCESS;
   link_shaders_init(&ctx,program);
   if(!gl_nir_link_glsl(&ctx,program) || !program->data->LinkStatus) {
      fprintf(stderr,"LINK FAIL: %s\n",program->data->InfoLog); return 1;
   }
   gl_program *prog=program->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program;
   nir_shader *nir=prog->nir;
   gl_nir_lower_buffers(nir,program);
   nir_lower_system_values(nir);
   nir_lower_io_passes(nir,false);
   struct st_context st={}; st.ctx=&ctx; st.screen=ctx.screen;
   // Match the unpacked state-uniform registration in st_glsl_to_nir_post_opts.
   nir_foreach_uniform_variable(var,nir) {
      for(unsigned i=0;i<var->num_state_slots;++i)
         _mesa_add_state_reference(prog->Parameters,var->state_slots[i].tokens);
   }
   st_update_state_param_locations(&ctx,prog,nir);
   st_finalize_nir(&st,prog,program,nir,false,false);
   nir_shader_gather_info(nir,nir_shader_get_entrypoint(nir));
   nir_validate_shader(nir,"captured fragment finalized");
   assert(!nir->info.spec);
   blob serialized; blob_init(&serialized); nir_serialize(&serialized,nir,true);
   FILE *out=fopen("ballot-fragment.nir","wb"); assert(out);
   assert(fwrite(serialized.data,serialized.size,1,out)==1); fclose(out); blob_finish(&serialized);
   printf("PASS compatibility fragment compile/link: %s\n",argv[2]);
   standalone_compiler_cleanup(program,&ctx);
}
