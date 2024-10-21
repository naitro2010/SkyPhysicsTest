#![feature(pointer_byte_offsets)]
extern crate ocl;
use ocl::{ProQue,Buffer,Kernel};
use windows::{core::PCSTR, Win32::System::Diagnostics::Debug::{OutputDebugStringA, OutputDebugStringW}};
use std::{borrow::Cow, ffi::CString};


#[derive(Debug)]
pub struct MUDGPUStruct
{
    ocl_queue:ProQue,
    ocl_kernel: Kernel,
    recalc_kernel: Kernel,
    ocl_orig_buffer: Buffer::<f32>,
    ocl_next_buffer: Buffer::<f32>,
    ocl_deform_positions: Buffer::<f32>,
    ocl_deform_vectors: Buffer::<f32>,
    falloff: f32,
    max_vertical_dist: f32,
    vertical_offset: f32
}

#[derive(Default,Debug)]
pub struct MUDFFI
{
    vertex_count: u32,
    vertex_stride: u32,
    gpu_struct: Option<MUDGPUStruct>,
}
#[derive(Debug,Copy,Clone)]
#[repr(C)]
pub struct MUDINIT
{
vertex_count: u32,
vertex_stride:u32,
triangle_count:u32,
triangles:*mut u16,
pos_offset:u32,
normal_offset:u32,
vertex_ptr:*mut f32,
falloff: f32,
max_vertical_dist:f32,
vertical_offset:f32,
min_dotprod:f32,
time: f32,
frequency: f32,
wave_speed_time_per_meter: f32,
sine_magnitude: f32,
chirp_multi: f32,
}
#[no_mangle]
pub unsafe extern fn destroy_mud (state:*mut MUDFFI)
{
    drop(Box::from_raw((state)));
}
#[no_mangle]
pub unsafe extern fn reset_mud (state:*mut MUDFFI) -> *mut MUDFFI
{
    let mut mud=Box::from_raw(state);

    return Box::into_raw(mud);
}
#[no_mangle]
pub unsafe extern fn update_mud (state:*mut MUDFFI,deform_pos_array: *mut f32,deform_vec_array: *mut f32, output_vertex_data: *mut f32,sink: f32, time: f32,frequency: f32, wave_speed_time_per_meter: f32, chirp_multi:f32) -> *mut MUDFFI
{
    let mut mud=Box::from_raw(state);
    let mut deform_positions=unsafe { std::slice::from_raw_parts(deform_pos_array,8)};
    let mut deform_vectors=unsafe { std::slice::from_raw_parts(deform_vec_array,8)};
    let mut gpu_struct_ref=mud.gpu_struct.as_mut().unwrap();
    gpu_struct_ref.ocl_deform_positions.write(&deform_positions[0..8]).enq().unwrap();
    gpu_struct_ref.ocl_deform_vectors.write(&deform_vectors[0..8]).enq().unwrap();
    gpu_struct_ref.ocl_kernel.set_arg("sink", sink).unwrap();
    gpu_struct_ref.ocl_kernel.set_arg("time", time).unwrap();
    gpu_struct_ref.ocl_kernel.set_arg("frequency", frequency).unwrap();
    gpu_struct_ref.ocl_kernel.set_arg("wave_speed_time_per_meter", wave_speed_time_per_meter).unwrap();
    gpu_struct_ref.ocl_kernel.set_arg("chirp_multi", chirp_multi).unwrap();
    unsafe {gpu_struct_ref.ocl_kernel.enq().unwrap()};
    unsafe {gpu_struct_ref.recalc_kernel.enq().unwrap()};
    gpu_struct_ref.ocl_queue.finish().unwrap();
    let mut output_vertex_slice:&mut[f32]=std::slice::from_raw_parts_mut(output_vertex_data,(mud.vertex_count*mud.vertex_stride) as usize/4);
    gpu_struct_ref.ocl_next_buffer.read(&mut output_vertex_slice[..]).enq().unwrap();
    gpu_struct_ref.ocl_queue.finish().unwrap();
    
    return Box::into_raw(mud);
}
fn debug_log(input_string: &str)
{
    //let mut input=CString::new(input_string.to_string()).unwrap();
    //unsafe { OutputDebugStringA(PCSTR(input.as_ptr() as *const u8)) };
}
const src:&str = r#"
__kernel void deform_mud(unsigned int vertex_stride,unsigned int pos_offset,unsigned int normal_offset,__global float * ocl_orig_buffer,__global float * ocl_next_buffer,__global float* local_deform_positions,__global float* local_deform_vectors, unsigned int deform_count, float max_vertical_deform_distance_meters, float falloff,
     float vertical_offset, float min_dotprod, float time, float frequency, float wave_speed_time_per_meter, float sink, float sine_magnitude, float chirp_multi) {
    float3 new_position=(float3)(ocl_orig_buffer[vertex_stride*get_global_id(0)+pos_offset],ocl_orig_buffer[vertex_stride*get_global_id(0)+pos_offset+1],ocl_orig_buffer[vertex_stride*get_global_id(0)+pos_offset+2]);
    float3 orig_position=new_position.xyz+(float3)(0.0,0.0,sink*69.99125119);
    new_position=(float3)(0.0,0.0,0.0);
    float new_count=0.0;
    for (unsigned int di=0; di<deform_count; di++) {

            float3 deform_pos=vload4(di,local_deform_positions).xyz;
            float3 deform_vec=normalize(vload4(di,local_deform_vectors).xyz);
            float dP=dot(deform_vec,(float3)(0.0,0.0,-1.0));
            if (dP < min_dotprod) {
                 deform_vec=normalize((deform_vec)+(float3)(0.0,0.0,-1.0)*(min_dotprod-dP));
            }
            float u=((orig_position.z-deform_pos.z)/deform_vec.z);
            float3 plane_point=(u*deform_vec.xyz)+deform_pos.xyz;
            if ((deform_pos.z-orig_position.z) <= 0.0) {
                deform_pos.z=orig_position.z+0.01;
            }
            if (length(plane_point.z-deform_pos.z) > 69.99125119*max_vertical_deform_distance_meters) {
                continue;
            }
            if (length(orig_position.z-deform_pos.z) > 69.99125119*max_vertical_deform_distance_meters) {
                continue;
            }
            float distance_from_plane_point=length(plane_point.xyz-orig_position.xyz)/69.99125119;
            // 1.0/(1.0+((x^2)*falloff))
            float dist_scale=1.0/(1.0+pow(distance_from_plane_point,(float)2.0)*falloff);           
            dist_scale=min(max(dist_scale,(float)0.0),(float)1.0);
            float3 voffset=normalize(deform_pos.xyz-plane_point.xyz)*vertical_offset*(float)69.99125119;
            float3 voffset2=normalize(deform_pos.xyz-orig_position.xyz)*vertical_offset*(float)69.99125119;
            float3 offset=(deform_pos.xyz-plane_point.xyz)+voffset;
            float3 offset2=(deform_pos.xyz-orig_position.xyz)+voffset2;
            float3 offset_vec=normalize(offset);
            float offset_len=length(offset);
            float3 offset3=offset_vec*length(offset)*dist_scale;
            float sine_vertical_offset=((sin((6.2831853071796*(frequency+chirp_multi*(distance_from_plane_point)))*(time+((distance_from_plane_point)/wave_speed_time_per_meter)))+1.0)*sine_magnitude*(dist_scale))/2.0;
            float3 test_new_point=orig_position.xyz+offset3+offset_vec*(sine_vertical_offset*(float)69.99125119);
            //test_new_point += plane_point-orig_position;
            if (isfinite(length(test_new_point.xyz-new_position.xyz))) {
                if (test_new_point.z >= new_position.z) {
                    new_position.xyz=test_new_point.xyz;
                    new_count = 1.0;
                }
            }
    }
    if (new_count >= 1.0) {
        new_position.xyz/=new_count;
    } else {
        new_position.xyz=orig_position.xyz;
    }
    vstore4((char4)(0,0,0,0),0,(__global char *)&ocl_next_buffer[get_global_id(0)*vertex_stride+normal_offset+0]);
    ocl_next_buffer[get_global_id(0)*vertex_stride+pos_offset+0]=new_position.x;
    ocl_next_buffer[get_global_id(0)*vertex_stride+pos_offset+1]=new_position.y;
    ocl_next_buffer[get_global_id(0)*vertex_stride+pos_offset+2]=new_position.z;
    
}
__kernel void recalculate_normals(unsigned int vertex_stride,unsigned int vertex_len,unsigned int triangles_len,unsigned int pos_offset,unsigned int normal_offset,__global unsigned short *ocl_triangle_indices,__global float4 * new_normals,__global float * ocl_next_buffer)
{
     for (unsigned int triangle_idx=0; triangle_idx < triangles_len-2; triangle_idx+=3) {
         unsigned int vertex_idx0=ocl_triangle_indices[triangle_idx];
         unsigned int vertex_idx1=ocl_triangle_indices[(triangle_idx+1)];
         unsigned int vertex_idx2=ocl_triangle_indices[(triangle_idx+2)];
         float4 pos0=vload4(0,&ocl_next_buffer[vertex_idx0*vertex_stride+pos_offset]);
         float4 pos1=vload4(0,&ocl_next_buffer[vertex_idx1*vertex_stride+pos_offset]);
         float4 pos2=vload4(0,&ocl_next_buffer[vertex_idx2*vertex_stride+pos_offset]);
         
         float3 normal=cross(pos1.xyz-pos0.xyz,pos2.xyz-pos0.xyz);
         new_normals[vertex_idx0].xyz+=normal;
         new_normals[vertex_idx1].xyz+=normal;
         new_normals[vertex_idx2].xyz+=normal;
         
     }
     for (unsigned int vertex_idx=0; vertex_idx < vertex_len; vertex_idx+=1)
     {
         char4 scaled_normal=(char4)(0,0,0,0);
         float4 scaled_normal_f4=0.0;
         scaled_normal_f4.xyz=round(normalize(new_normals[vertex_idx].xyz)*127);
         scaled_normal.x=(char)scaled_normal_f4.x;
         scaled_normal.y=(char)scaled_normal_f4.y;
         scaled_normal.z=(char)scaled_normal_f4.z;
         vstore4(scaled_normal,0, (__global char*)&ocl_next_buffer[vertex_idx*vertex_stride+normal_offset+0]);
     }
     
}
"#;

#[no_mangle]
pub unsafe extern fn init_mud ( mud_init:*mut MUDINIT) -> *mut MUDFFI
{

    let mut mud=MUDFFI::default();
   // float3 test_new_point=(((deform_pos+(normal_offset*vertical_offset*(float)69.99125119))-plane_point.xyz)*dist_scale)+orig_position.xyz;
    let mut mistruct:&MUDINIT= &mut *mud_init;
    debug_log("Making Queue");
    let pro_que = ProQue::builder()
        .src(src)
        .dims(mistruct.vertex_count)
        .build().unwrap();
    let mut deform_count=2;
    let mut ocl_triangle_indices=pro_que.buffer_builder::<u16>().len(mistruct.triangle_count).build().unwrap();
    debug_log("Making B1");
    let mut ocl_orig_buffer=pro_que.buffer_builder::<f32>().len(mistruct.vertex_count*(mistruct.vertex_stride/4)).build().unwrap();
    debug_log("Making B2");
    let mut ocl_deform_positions=pro_que.buffer_builder::<f32>().len(deform_count*4).build().unwrap();
    debug_log("Making B3");
    let mut ocl_deform_vectors=pro_que.buffer_builder::<f32>().len(deform_count*4).build().unwrap();
    debug_log("Making B4");
    let mut ocl_next_buffer=pro_que.buffer_builder::<f32>().len(mistruct.vertex_count*(mistruct.vertex_stride/4)).build().unwrap();
    debug_log("Making K");
    let deform_mud_kernel = pro_que.kernel_builder("deform_mud")
    .arg(mistruct.vertex_stride/4 as u32)
    .arg(mistruct.pos_offset/4 as u32)
    .arg(mistruct.normal_offset/4 as u32)
    .arg(&ocl_orig_buffer)
    .arg(&mut ocl_next_buffer)
    .arg(&ocl_deform_positions)
    .arg(&ocl_deform_vectors)
    .arg(deform_count as u32)
    .arg(mistruct.max_vertical_dist)
    .arg(mistruct.falloff)
    .arg(mistruct.vertical_offset)
    .arg(mistruct.min_dotprod)
    .arg_named("time",mistruct.time)
    .arg_named("frequency",mistruct.frequency)
    .arg_named("wave_speed_time_per_meter",mistruct.wave_speed_time_per_meter)
    .arg_named("sink",0.0f32)
    .arg_named("sine_magnitude", mistruct.sine_magnitude)
    .arg_named("chirp_multi", mistruct.chirp_multi)    
    .build().unwrap();
    let mut ocl_normals_temp_buffer=pro_que.buffer_builder::<f32>().len(mistruct.vertex_count*4).build().unwrap();
    let recalculate_normals_kernel = pro_que.kernel_builder("recalculate_normals")
    .arg(mistruct.vertex_stride/4 as u32)
    .arg(mistruct.vertex_count as u32)
    .arg(mistruct.triangle_count as u32)
    .arg(mistruct.pos_offset/4 as u32)
    .arg(mistruct.normal_offset/4 as u32)
    .arg(&ocl_triangle_indices)
    .arg(&mut ocl_normals_temp_buffer)
    .arg(&mut ocl_next_buffer)
    .global_work_size(1 as u32)
    .build().unwrap();
    debug_log("Writing B1");
    ocl_orig_buffer.write(std::slice::from_raw_parts::<f32>(mistruct.vertex_ptr,(mistruct.vertex_count as usize * ((mistruct.vertex_stride as usize))) /4)).enq().unwrap();
    
    ocl_triangle_indices.write(std::slice::from_raw_parts::<u16>(mistruct.triangles,mistruct.triangle_count as usize)).enq().unwrap();
    debug_log("Writing B4");
    ocl_next_buffer.write(std::slice::from_raw_parts::<f32>(mistruct.vertex_ptr,(mistruct.vertex_count as usize * ((mistruct.vertex_stride as usize)))/4)).enq().unwrap();

    mud.vertex_count=mistruct.vertex_count;
    mud.vertex_stride=mistruct.vertex_stride;
    mud.gpu_struct=Some(MUDGPUStruct{ocl_queue:pro_que,ocl_kernel:deform_mud_kernel,recalc_kernel:recalculate_normals_kernel,ocl_orig_buffer:ocl_orig_buffer,ocl_next_buffer:ocl_next_buffer,ocl_deform_positions:ocl_deform_positions,ocl_deform_vectors:ocl_deform_vectors,falloff:mistruct.falloff,max_vertical_dist:mistruct.max_vertical_dist,vertical_offset:mistruct.vertical_offset});
    debug_log("OCL Ready");
    return Box::into_raw(Box::new(mud));
    
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn testone() {
        
    let mut vertex_stride=32;
    let mut normal_offset=0;
    let mut pos_offset=16;
    let mut deform_count=2;
    let mut max_vertical_dist=1.0f32;
    let mut falloff=0.1f32;
    let mut vertical_offset=0.1f32;
    let mut min_dotprod=0.7f32;
    let mut vertex_count=4096;
    let mut triangle_count=4096*3;
        let pro_que = ProQue::builder()
        .src(src)
        .dims(vertex_count)
        .build().unwrap();
    let mut deform_count=2;
    let mut ocl_triangle_indices=pro_que.buffer_builder::<u16>().len(triangle_count).build().unwrap();
    debug_log("Making B1");
    let mut ocl_orig_buffer=pro_que.buffer_builder::<f32>().len(vertex_count*(vertex_stride/4)).build().unwrap();
    debug_log("Making B2");
    let mut ocl_deform_positions=pro_que.buffer_builder::<f32>().len(deform_count*4).build().unwrap();
    debug_log("Making B3");
    let mut ocl_deform_vectors=pro_que.buffer_builder::<f32>().len(deform_count*4).build().unwrap();
    debug_log("Making B4");
    let mut ocl_next_buffer=pro_que.buffer_builder::<f32>().len(vertex_count*(vertex_stride/4)).build().unwrap();
    let mut ocl_normals_temp_buffer=pro_que.buffer_builder::<f32>().len(vertex_count*4).build().unwrap();
    debug_log("Making K");

        let deform_mud_kernel = pro_que.kernel_builder("deform_mud")
        
        .arg(vertex_stride/4 as u32)
        .arg(pos_offset/4 as u32)
        .arg(normal_offset/4 as u32)
        .arg(&ocl_orig_buffer)
        .arg(&mut ocl_next_buffer)
        .arg(&ocl_deform_positions)
        .arg(&ocl_deform_vectors)
        .arg(deform_count as u32)
        .arg(max_vertical_dist)
        .arg(falloff)
        .arg(vertical_offset)
        .arg(min_dotprod)
        .arg_named("time",0f32)
        .arg_named("frequency",0f32)
        .arg_named("wave_speed_time_per_meter",0f32)
        .arg_named("sink",0f32)
        .arg_named("sine_magnitude", 0.3f32)
        .arg_named("chirp_multi", 0.3f32)
        .build().unwrap();
        let recalculate_normals_kernel = pro_que.kernel_builder("recalculate_normals")
        .arg(vertex_stride/4 as u32)
        .arg(vertex_count as u32)
        .arg(triangle_count as u32)
        .arg(pos_offset/4 as u32)
        .arg(normal_offset/4 as u32)
        .arg(&ocl_triangle_indices)
        .arg(&mut ocl_normals_temp_buffer)
        .arg(&mut ocl_next_buffer)
        .global_work_size(1 as u32)
        .build().unwrap();
    }

}
