#pragma warning(disable: 4100 4189)
#include <iostream>
#include <cstdarg>
#include <thread>
#include <detours/detours.h>
#include <d3d11.h>
#include <mudbindings.h>
#define DLLEXPORT __declspec(dllexport)

typedef struct
{
	bool mud_ready;
	MUDFFI* mud_ffi;
	MUDINIT mudinit;
	uint8_t* originalVertexData;
} MudDeformState;
std::recursive_mutex g_mudmutex;
std::set<uint64_t> g_vtableset;
std::map<RE::TESObjectREFR*, MudDeformState> g_mudstate;
std::map<RE::TESObjectREFR*, float> g_mudtimes;
void* OriginalUpdateAnimationPtr = nullptr;
void* OriginalUpdateAnimationPtrPlayer = nullptr;
static ID3D11DeviceContext * NewDeferredContext;
static void* OriginalSwapPresent;
HRESULT SwapPresentHook(IDXGISwapChain* SC, unsigned int interval, unsigned int flags) {
	HRESULT(*SwapPresentOrig)(IDXGISwapChain * SC, unsigned int interval, unsigned int flags) = (HRESULT(*)(IDXGISwapChain * SC, unsigned int interval, unsigned int flags)) OriginalSwapPresent;
	if (NewDeferredContext != nullptr) {
		ID3D11CommandList* list;
		NewDeferredContext->FinishCommandList(FALSE,&list);
		ID3D11DeviceContext* context = nullptr;
		RE::BSGraphics::Renderer::GetSingleton()->GetDevice()->GetImmediateContext(&context);
		context->ExecuteCommandList(list, FALSE);
		list->Release();
		context->Release();

	}
	return SwapPresentOrig(SC, interval, flags);

}
void InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		util::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	const auto level = a_level;

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
}
RE::NiAVObject* FindNiObjectName(RE::NiNode* obj,std::string FindName)
{
	if (!obj) {
		return nullptr;
	}
	for (auto aviter : obj->GetChildren())
	{
		if (!aviter) {
			continue;
		}
		logger::info("{}", aviter->name.c_str());
		if (aviter->name.contains(FindName)) {
			return aviter.get();
		}
		if (aviter->AsNode()) {
			if ( RE::NiAVObject* found=FindNiObjectName(aviter->AsNode(), FindName)) {
				return found;
			}
		}
	}
	return nullptr;
}
void UpdateMudPhysics(RE::Actor* ref, float delta, uint64_t arg3, uint64_t arg4);
void UpdateMudPhysicsPlayer(RE::Actor* ref, float delta, uint64_t arg3, uint64_t arg4)
{
	UpdateMudPhysics(ref,delta,arg3,arg4);
	void (*OriginalUpdateAnimation)(RE::TESObjectREFR*, float dt, uint64_t arg3, uint64_t arg4) = (void (*)(RE::TESObjectREFR* objRef, float dt, uint64_t, uint64_t))(OriginalUpdateAnimationPtrPlayer);
	return OriginalUpdateAnimation(ref, delta, arg3, arg4);
}
void UpdateMudPhysicsActor(RE::Actor* ref, float delta, uint64_t arg3, uint64_t arg4)
{
	UpdateMudPhysics(ref, delta,arg3,arg4);
	void (*OriginalUpdateAnimation)(RE::TESObjectREFR*, float dt, uint64_t arg3, uint64_t arg4) = (void (*)(RE::TESObjectREFR* objRef, float dt, uint64_t, uint64_t))(OriginalUpdateAnimationPtr);
	return OriginalUpdateAnimation(ref, delta, arg3, arg4);
}
void UpdateMudPhysics(RE::Actor* ref, float delta,uint64_t arg3,uint64_t arg4)
{

	std::lock_guard<std::recursive_mutex> lock(g_mudmutex);
	auto bipedanim = ref->GetBiped1(false);
	logger::info("UpdateAnimation delta {}", delta);
	if (bipedanim.get() == nullptr) {
		return;
	}
	if (ref->Get3D1(false) == nullptr) {
		return;
	}
	
	RE::NiAVObject *LFoot=ref->Get3D1(false)->GetObjectByName(RE::BSFixedString("CME L Foot [Lft ]"));
	RE::NiAVObject *RFoot=ref->Get3D1(false)->GetObjectByName(RE::BSFixedString("CME R Foot [Rft ]"));
	RE::NiAVObject* RKnee = ref->Get3D1(false)->GetObjectByName(RE::BSFixedString("CME R Knee [RKne]"));
	RE::NiAVObject* LKnee = ref->Get3D1(false)->GetObjectByName(RE::BSFixedString("CME L Knee [LKne]"));
	if (g_mudstate.contains(ref->AsReference())) {
		MudDeformState &state = g_mudstate[ref->AsReference()];
		RE::NiAVObject* mud_geometry_av = nullptr;
		if (mud_geometry_av == nullptr) {
			for (int i = 0; i < 42; i++) {
				auto& bpo = ref->GetBiped1(false)->objects[i];
				if (!bpo.addon || !bpo.part || !bpo.partClone) {
					continue;
				}
				
				logger::info("{}", bpo.part->model.c_str());
				if (!bpo.part->model.contains("mud.nif")) {
					continue;
				}
				
				
				if ((bpo.partClone)) {
					mud_geometry_av = bpo.partClone.get();
						
				}
				
			}
		}
		if (mud_geometry_av == nullptr) {
			for (int i = 0; i < 42; i++) {

				auto& bpo = ref->GetBiped1(false)->bufferedObjects[i];
				if (!bpo.addon || !bpo.part || !bpo.partClone) {
					continue;
				}

				logger::info("{}", bpo.part->model.c_str());
				if (!bpo.part->model.contains("mud.nif")) {
					continue;
				}

				if ((bpo.partClone)) {
					mud_geometry_av = bpo.partClone.get();
				}
				
			}
		}
		//bipedanim->root->GetObjectByName(RE::BSFixedString("MudGeometry"));

		if (mud_geometry_av == nullptr || mud_geometry_av->AsGeometry() == nullptr) {
			state.mud_ready = false;
			return;
		} else {
			RE::BSGeometry* mud_shape = mud_geometry_av->AsGeometry();
			if (!state.mud_ready) {
				if (mud_shape->GetGeometryRuntimeData().skinInstance != nullptr) {
					if (mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition != nullptr) {
						if (mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->vertexCount > 0) {
							uint32_t vertexCount = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->vertexCount;
							uint32_t vertexSize = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].vertexDesc.GetSize();
							state.originalVertexData = (uint8_t*)malloc(vertexSize * vertexCount);
							memcpy(state.originalVertexData, mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].buffData->rawVertexData, vertexSize * vertexCount);
							uint8_t* vertex_positions=mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].buffData->rawVertexData + mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_POSITION);
							for (unsigned int vidx = 0; vidx < vertexCount; vidx++) {
								uint32_t offset=vidx * vertexSize;
								DirectX::XMFLOAT4 &vertexPosition = *(DirectX::XMFLOAT4*)(vertex_positions + offset);
								//logger::info("Vertex {} pos {} {} {}", vidx, vertexPosition.x, vertexPosition.y, vertexPosition.z);
								//vertexPosition.x *= 0.1f;
								//vertexPosition.y *= 0.1f;
								//vertexPosition.z *= 0.1f;
								
							}
							uint32_t triCount = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].triangles;
							uint16_t* triangles = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].triList;
							auto& mudinit = state.mudinit;
							mudinit.time += delta;
							mudinit.vertex_count = vertexCount;
							mudinit.vertex_stride = vertexSize;
							mudinit.triangle_count = triCount*3;
							mudinit.triangles = triangles;

							mudinit.pos_offset = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_POSITION);
							mudinit.normal_offset = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_NORMAL);
							mudinit.tangent_offset = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_BINORMAL);
							mudinit.vertex_ptr = (float*)mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].buffData->rawVertexData;
							if (!g_mudtimes.contains(ref->AsReference())) {
								g_mudtimes.insert(std::pair(ref->AsReference(), 0.0f));
								float &time = g_mudtimes.at(ref->AsReference());
							}
							state.mud_ffi = init_mud(&mudinit);							
							
							
							//state.mud_bodies.push_back(state.physics_system.GetBodyInterface().CreateAndAddBody()
							state.mud_ready = true;

						}
					}
				}
				//float sine_vertical_offset = (sin((6.2831853071796 * frequency) * (time + ((length(plane_point.xyz - orig_position.xyz) / 69.99125119) * wave_speed_time_per_meter))) + 1.0);
				
			} else {
				if (mud_shape->GetGeometryRuntimeData().skinInstance != nullptr) {
					if (mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition != nullptr) {
						
							
								uint32_t vertexCount = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->vertexCount;
								uint32_t vertexSize = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].vertexDesc.GetSize();
								//ID3D11Device* device = nullptr;
								ID3D11Buffer* VB = ((ID3D11Buffer*)(mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].buffData->vertexBuffer));
								//VB->GetDevice(&device);
								
								
								if (LFoot && RFoot && LKnee && RKnee) {
									RE::NiPoint3 LFoot_to_mud = mud_shape->world.rotate.Transpose() * (LFoot->world.translate - mud_shape->world.translate);
									RE::NiPoint3 RFoot_to_mud = mud_shape->world.rotate.Transpose() * (RFoot->world.translate - mud_shape->world.translate);
									RE::NiPoint3 LFoot_down_to_mud = LFoot->world.translate - LKnee->world.translate;
									RE::NiPoint3 RFoot_down_to_mud = RFoot->world.translate - RKnee->world.translate;
									
									LFoot_down_to_mud = mud_shape->world.rotate.Transpose() * LFoot_down_to_mud;
									RFoot_down_to_mud = mud_shape->world.rotate.Transpose() * RFoot_down_to_mud;
									
									float deform_positions[8];
									deform_positions[4 * 0 + 0] = RFoot_to_mud.x;
									deform_positions[4 * 0 + 1] = RFoot_to_mud.y;
									deform_positions[4 * 0 + 2] = RFoot_to_mud.z;
									deform_positions[4 * 0 + 3] = 0.0;
									deform_positions[4 * 1 + 0] = LFoot_to_mud.x;
									deform_positions[4 * 1 + 1] = LFoot_to_mud.y;
									deform_positions[4 * 1 + 2] = LFoot_to_mud.z;
									deform_positions[4 * 1 + 3] = 0.0;
									float deform_vectors[8];
									deform_vectors[4 * 0 + 0] = 0.0;   //  RFoot_down_to_mud.x;
									deform_vectors[4 * 0 + 1] = 0.0;  //RFoot_down_to_mud.y;
									deform_vectors[4 * 0 + 2] = -1.0;  // RFoot_down_to_mud.z;
									deform_vectors[4 * 0 + 3] = 0.0;
									deform_vectors[4 * 1 + 0] = 0.0;
									deform_vectors[4 * 1 + 1] = 0.0;
									deform_vectors[4 * 1 + 2] = -1.0;
									deform_vectors[4 * 1 + 3] = 0.0;
									if (g_mudtimes.contains(ref->AsReference())) {
										g_mudtimes.insert(std::pair(ref->AsReference(), 0.0f));
										auto& mudinit = state.mudinit;
										float& time = g_mudtimes.at(ref->AsReference());
										time += delta;
										float sink = (fmod(time,30.0f)/30.0f)*0.7f;
										float frequency = mudinit.frequency;
										state.mud_ffi = update_mud(state.mud_ffi, deform_positions, deform_vectors, (float*)mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].buffData->rawVertexData, sink, time, frequency, mudinit.wave_speed_time_per_meter,mudinit.chirp_multi);
									}
									uint8_t* vertex_positions = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].buffData->rawVertexData + mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_POSITION);
									/* for (unsigned int vidx = 0; vidx < vertexCount; vidx++) {
										uint32_t offset = vidx * vertexSize;
										DirectX::XMFLOAT4& vertexPosition = *(DirectX::XMFLOAT4*)(vertex_positions + offset);
										if (vertexPosition.z > 0.00001) {
											logger::info("Vertex {} pos {} {} {}", vidx, vertexPosition.x, vertexPosition.y, vertexPosition.z);
										}
										//vertexPosition.x *= 0.1f;
										//vertexPosition.y *= 0.1f;
										//vertexPosition.z *= 0.1f;
									}*/
									uint8_t* vertexdata = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].buffData->rawVertexData;
									//RE::BSGraphics::Renderer::GetSingleton()->Lock();
									
									//ID3D11DeviceContext* context;
									
									
									//device->GetImmediateContext(&context);
									if (NewDeferredContext == nullptr) {
										RE::BSGraphics::Renderer::GetSingleton()->GetDevice()->CreateDeferredContext(0, &NewDeferredContext);
										OriginalSwapPresent = (*(void***)RE::BSGraphics::Renderer::GetSingleton()->GetCurrentRenderWindow()->swapChain)[8];
										uint64_t SwapPresentPtr = (uint64_t)(void*)SwapPresentHook;
										REL::safe_write((uintptr_t) & ((*(void***)RE::BSGraphics::Renderer::GetSingleton()->GetCurrentRenderWindow()->swapChain)[8]), &SwapPresentPtr, 8);
									}
									NewDeferredContext->UpdateSubresource(VB, 0, NULL, vertexdata, vertexCount * vertexSize, 0);
									//device->Release();
									//RE::BSGraphics::Renderer::GetSingleton()->Unlock();
								}
								
						
					}
				}
			}
		}
	}

}
void DetachMudPhysics(RE::StaticFunctionTag*, RE::Actor* objRef)
{
	std::lock_guard<std::recursive_mutex> lock(g_mudmutex);
	if (!objRef) {
		return;
	}
	if (RE::Actor* ref = objRef->As<RE::Actor>()) {
		RE::NiAVObject* mud_geometry_av = nullptr;
		if (mud_geometry_av == nullptr) {
			for (int i = 0; i < 42; i++) {
				auto& bpo = ref->GetBiped1(false)->objects[i];
				if (!bpo.addon || !bpo.part || !bpo.partClone) {
					continue;
				}

				logger::info("{}", bpo.part->model.c_str());
				if (!bpo.part->model.contains("mud.nif")) {
					continue;
				}

				if ((bpo.partClone)) {
					mud_geometry_av = bpo.partClone.get();
				}
			}
		}
		if (mud_geometry_av == nullptr) {
			for (int i = 0; i < 42; i++) {
				auto& bpo = ref->GetBiped1(false)->bufferedObjects[i];
				if (!bpo.addon || !bpo.part || !bpo.partClone) {
					continue;
				}

				logger::info("{}", bpo.part->model.c_str());
				if (!bpo.part->model.contains("mud.nif")) {
					continue;
				}

				if ((bpo.partClone)) {
					mud_geometry_av = bpo.partClone.get();
				}
			}
		}
		if (g_mudstate.contains(objRef)) {
			if (mud_geometry_av) {
				RE::BSGeometry* mud_shape = mud_geometry_av->AsGeometry();
				if (mud_shape) {
					if (g_mudstate[objRef].mud_ready) {
						if (mud_shape->GetGeometryRuntimeData().skinInstance != nullptr) {
							if (mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition != nullptr) {
								if (mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->vertexCount > 0) {
									uint32_t vertexCount = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->vertexCount;
									uint32_t vertexSize = mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].vertexDesc.GetSize();
									memcpy(mud_shape->GetGeometryRuntimeData().skinInstance->skinPartition->partitions[0].buffData->rawVertexData, g_mudstate[objRef].originalVertexData, vertexSize * vertexCount);
								}
							}
						}
					}
				}
			}
			free(g_mudstate[objRef].originalVertexData);
			destroy_mud(g_mudstate[objRef].mud_ffi);
			g_mudstate.erase(objRef);
		}
		if (g_mudtimes.contains(objRef)) {
			g_mudtimes.erase(objRef);
		}
	}
}
void AttachMudPhysics(RE::StaticFunctionTag*, RE::Actor* objRef, float falloff,float sine_magnitude,float frequency, float wave_speed_time_per_meter, float chirp_multi)
{
	
	std::lock_guard<std::recursive_mutex> lock(g_mudmutex);
	auto& trampoline = SKSE::GetTrampoline();
	if (!objRef) {
		return;
	}
	if (OriginalUpdateAnimationPtr == nullptr) {
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		OriginalUpdateAnimationPtr = (void*)REL::Offset(0x66aad0).address();
		
		DetourAttach(&OriginalUpdateAnimationPtr, UpdateMudPhysicsActor);
		DetourTransactionCommit();
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		OriginalUpdateAnimationPtrPlayer = (void*)REL::Offset(0x7393a0).address();

		DetourAttach(&OriginalUpdateAnimationPtrPlayer, UpdateMudPhysicsPlayer);
		DetourTransactionCommit();
	}
	if (RE::Actor * actor=objRef->As<RE::Actor>()) {
		
		uint64_t** vtable = (uint64_t**)*(uint64_t*)objRef;
		MudDeformState state;
		state.mud_ready = false;
		auto& mudinit = state.mudinit;
		mudinit.time = 0.0;

		mudinit.falloff = falloff;
		mudinit.max_vertical_dist = 2.0f;
		mudinit.sine_magnitude = sine_magnitude;
		mudinit.vertical_offset = 0.25f;
		mudinit.min_dotprod = 0.7f;
		mudinit.frequency = frequency;
		mudinit.wave_speed_time_per_meter = wave_speed_time_per_meter;
		mudinit.chirp_multi = chirp_multi;
		if (!g_mudstate.contains(objRef)) {
			g_mudstate.insert_or_assign(objRef, state);
		}
		
	}
	
		
}
bool BindPapyrusFunctions(RE::BSScript::IVirtualMachine* vm)
{
	vm->RegisterFunction("AttachMudPhysics", "SkyPhysics", AttachMudPhysics);
	vm->RegisterFunction("DetachMudPhysics", "SkyPhysics", DetachMudPhysics);
	return true;
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	logger::info("Loaded plugin {} {}", Plugin::NAME, Plugin::VERSION.string());
	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(512);
	
	SKSE::GetPapyrusInterface()->Register(BindPapyrusFunctions);
	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary(true);
	v.HasNoStructUse();
	return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}