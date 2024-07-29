#include <Windows.h>
#include <stdio.h>
#include <intrin.h>
#include <string>
#include <vector>

#include <SharedCheatLibrary/rust_internal.hpp>

typedef PVOID(*fnStartCoroutine)(PVOID monoBehavior, PVOID param2);
typedef void (*fnClientInput)(BasePlayer* plly, uintptr_t state);
typedef String* (*fnConsoleRun)(ConsoleSystem::Option* optiom, String* str, Array<System::Object_*>* args);
typedef void (*fnOnLand)(BasePlayer* ply, float vel);
typedef void (*fnPlay)(ViewModel* viewmodel, String* name, int layer);
typedef void (*fnProcessAttack)(BaseEntity* melee, HitTest* hittest);

API PVOID StartCoroutine_hk(PVOID monoBehavior, PVOID param2);

EjectionTracker tracker;
bool did_reload = false;
bool just_shot = false;
float fixed_time_last_shot = 0;
int lastAmmo = 0;

void ClientInput_hk(BasePlayer* plly, uintptr_t state) {
    if (!plly) {
        return ((fnClientInput)InternalJTableManager::target(INTERNAL_JTABLE_TARGETS::ClientInput))(plly, state);
    }
    if (plly->userID() == LocalPlayer::Entity()->userID()) {
        auto heldItem = plly->GetHeldItem();
        if (heldItem->info()->IsWeaponProjectileReloadable()) {
            auto held = heldItem->heldEntity<BaseProjectile>();
            held->automatic() = true;

            float time_since_last_shot = 0;
            if (!just_shot) {
                just_shot = held->primaryMagazine()->contents() < lastAmmo;
                if (just_shot) {
                    fixed_time_last_shot = Time::fixedTime();
                    did_reload = false;
                }
            }

            lastAmmo = held->primaryMagazine()->contents();
            if (!did_reload)
                time_since_last_shot = (Time::fixedTime() - fixed_time_last_shot);

            if (just_shot && (time_since_last_shot > 0.2f))
            {
                held->ServerRPC("StartReload");
                held->SendSignalBroadcast(BaseEntity::Signal::Reload);
                fixed_time_last_shot = Time::fixedTime();
                just_shot = false;
            }
            if (time_since_last_shot > (held->reloadTime() - (held->reloadTime() / 10))
                && !did_reload)
            {
                held->ServerRPC("Reload");
                did_reload = true;
                time_since_last_shot = 0;
            }
        }

        auto closestPlayer = (BasePlayer*)plly->clientEntities()->FindFirstClosest(STATIC_CRC32("BasePlayer"), LocalPlayer::Entity(), 4.f);
        if (closestPlayer && closestPlayer->HasPlayerFlag(PlayerFlags::Wounded)) {
            closestPlayer->ServerRPC("RPC_Assist");
            const String* name = closestPlayer->_displayName();
            std::wstring wName(name->buffer, name->len);
        }

        PlayerWalkMovement* playerMovement = plly->movement();
        if (!playerMovement)
            return;

        playerMovement->groundAngle() = 0.f;
        playerMovement->groundAngleNew() = 0.f;

        CapsuleCollider* capsuleCollider = playerMovement->capsule();
        if (!capsuleCollider)
            return;

        capsuleCollider->set_radius(0.44f);

        auto list = TOD_Sky::instances();
        if (list) {
            for (int j = 0; j < list->size; j++) {
                auto sky = (TOD_Sky*)list->get(j);
                if (!sky)
                    continue;

                float amb = 5.f;

                sky->Day()->AmbientMultiplier() = 1.f;
                sky->Night()->AmbientMultiplier() = amb;
            }
        }

        const float vr = 100.0f;
        const float vg = 0.0f;
        const float vb = 255.0f;
        const float nr = 0.0f;
        const float ng = 100.0f;
        const float nb = 200.0f;
        auto entityList = BaseNetworkable::get_entitylist();

        for (int i = 0; entityList && i < entityList->size; i++) {
            auto current_object = *reinterpret_cast<Object**>((char*)entityList->buffer + (0x20 + (i * sizeof(uint64_t))));
            if (!current_object)
            {
                continue;
            }

            auto base_object = current_object->m_CachedPtr();
            if (!base_object)
            {
                continue;
            }

            auto object = *reinterpret_cast<uintptr_t*>(base_object + 0x30);
            if (!object)
            {
                continue;
            }

            const auto tag = *reinterpret_cast<std::uint16_t*>(object + 0x54);
            if (tag == 6)
            {
                auto object_class = *reinterpret_cast<uintptr_t*>(object + 0x30);
                if (!object_class)
                    return;

                auto entity = *reinterpret_cast<uintptr_t*>(object_class + 0x18);
                if (!entity)
                    return;

                auto player = *reinterpret_cast<BasePlayer**>(entity + 0x28);
                if (!reinterpret_cast<uintptr_t>(player))
                    return;

                if (InternalModule::ShouldEject() || !InternalModule::features().rust.bChamsOn) {
                    if (InternalModule::ShouldEject())
                        tracker.Notify(ClientInput);
                    unity::undo_chams(player);
                }
                else {
                    //unity::do_chams(player, vr, vg, vb, nr, ng, nb);
                }
            }
        }
    }
    ((fnClientInput)InternalJTableManager::target(INTERNAL_JTABLE_TARGETS::ClientInput))(plly, state);
    LocalPlayer::Entity()->add_modelstate_flags(ModelState::Flags::OnGround | ModelState::Flags::Sprinting);
}

String* ConsoleRun_hk(ConsoleSystem::Option* opt, String* str, Array<System::Object_*>* args) {
    if (opt->IsFromServer()) {
        if (str->buffer) {
            auto string = std::wstring(str->buffer);
            printf("Command sent from server: %ws\n", string.c_str());
            //if (string.find(L"noclip") != std::wstring::npos ||
            //    string.find(L"debugcamera") != std::wstring::npos ||
            //    string.find(L"admintime") != std::wstring::npos ||
            //    string.find(L"camlerp") != std::wstring::npos ||
            //    string.find(L"camspeed") != std::wstring::npos) {
            //
            //    str = String::New("");
            //}
        }
    }

    fnConsoleRun ConsoleRun = (fnConsoleRun)InternalJTableManager::target(INTERNAL_JTABLE_TARGETS::ConsoleSystemRun);
    return ConsoleRun(opt, str, args);
}

void OnLand_hk(BasePlayer* ply, float vel) {

}

void RegisterForCulling_hk(BaseEntity* baseEnt)
{
    if (!baseEnt->IsPlayer()) {
        auto RegisterForCulling = reinterpret_cast<void(*)(BaseEntity*)>(InternalJTableManager::target(INTERNAL_JTABLE_TARGETS::RegisterForCulling));
        return RegisterForCulling(baseEnt);
    }
}

void HandleJumping_hk(PlayerWalkMovement* a1, ModelState* state, bool wantsJump, bool jumpInDirection = false) {
    if (!wantsJump)
        return;

    a1->grounded() = (a1->climbing() = (a1->sliding() = false));
    state->set_ducked(false);
    a1->jumping() = true;
    state->set_jumped(true);
    a1->jumpTime() = Time::time();
    a1->ladder() = nullptr;

    Vector3 curVel = a1->body()->velocity();
    a1->body()->set_velocity({ curVel.x, 10, curVel.z });
    return;
}

void ProcessAttack_hk(BaseEntity* melee, HitTest* hittest) {
    fnProcessAttack procAtkOrig = ((fnProcessAttack)InternalJTableManager::target(ProcessAttack));
    auto hit_entity = (BaseEntity*)hittest->HitEntity();
    if (!hit_entity) {
        return procAtkOrig(melee, hittest);
    }

    auto nameHash = hit_entity->class_name_hash();
    
    if (nameHash == STATIC_CRC32("OreResourceEntity")) {
        auto hit_entity_transform = hit_entity->transform();
        auto orePos = hit_entity_transform->position();
        auto local = LocalPlayer::Entity()->ClosestPoint(orePos);
        auto dist = math::ManhattanDistance(local, orePos);
        auto closest = melee->clientEntities()->FindFirstClosest(STATIC_CRC32("OreHotSpot"), LocalPlayer::Entity(), 2.f);
        if (!closest)
            return procAtkOrig(melee, hittest);

        Transform* hot_spot_transform = closest->transform();
        if (hot_spot_transform)
        {
            hittest->HitEntity() = hit_entity;
            hittest->HitTransform() = hot_spot_transform;
            auto hotspot_position = hot_spot_transform->position();
            auto closest_object_transform = closest->transform();
            auto inverse_point = closest_object_transform->InverseTransformPoint(hotspot_position);
            hittest->HitPoint() = inverse_point;
        }
    }
    else if (nameHash == STATIC_CRC32("TreeEntity")) {
        auto hit_entity_transform = hit_entity->transform();
        auto treePos = hit_entity_transform->position();
        auto inverse_point = hit_entity_transform->InverseTransformPoint(treePos + Vector3(0.f, 1.f, 0.f));
        auto local = LocalPlayer::Entity()->ClosestPoint(treePos);
        auto dist = math::ManhattanDistance(local, treePos);
        BaseEntity* closest_object = (BaseEntity*)melee->clientEntities()->FindFirstClosest(STATIC_CRC32("TreeMarker"), LocalPlayer::Entity(), 2.f);

        if (!closest_object)
        {
            hittest->HitDistance() = dist;
            hittest->HitPoint() = inverse_point;
        }
        else
        {
            auto pos3 = closest_object->transform()->position();
            auto pos3_inverse_point = hit_entity_transform->InverseTransformPoint(pos3);
            hittest->HitDistance() = dist;
            hittest->HitPoint() = pos3_inverse_point;
            TreeEntity* tree = (TreeEntity*)hit_entity;
            tree->hitDirection() = (Vector3(treePos.x, 0, treePos.z) - Vector3(pos3.x, 0, pos3.z));
        }
    }
    return procAtkOrig(melee, hittest);
}

VOID OnInit(InternalModule& mainMod) {
    hookedFunctionsInit();

    if (BasePlayer::OnLand_) {
        mainMod.cmd.QueueHook(BasePlayer::OnLand_, OnLand_hk, InternalJTableManager::offset(OnLand));
    }
    if (ConsoleSystem::Run_) {
        mainMod.cmd.QueueHook(ConsoleSystem::Run_, ConsoleRun_hk, InternalJTableManager::offset(ConsoleSystemRun));
    }
    if (BasePlayer::ClientInput_) {
        tracker.Register(ClientInput);
        InternalModule::features().rust.bChamsOn = true;
        mainMod.cmd.QueueHook(BasePlayer::ClientInput_, ClientInput_hk, InternalJTableManager::offset(ClientInput));
    }
    if (BasePlayer::RegisterForCulling_) {
        mainMod.cmd.QueueHook(BasePlayer::RegisterForCulling_, RegisterForCulling_hk, InternalJTableManager::offset(RegisterForCulling));
    }
    if (PlayerWalkMovement::HandleJumping_) {
        mainMod.cmd.QueueHook(PlayerWalkMovement::HandleJumping_, HandleJumping_hk, InternalJTableManager::offset(HandleJumping));
    }
    if (BaseMelee::ProcessAttack_) {
        mainMod.cmd.QueueHook(BaseMelee::ProcessAttack_, ProcessAttack_hk, InternalJTableManager::offset(ProcessAttack));
    }
}

API PVOID StartCoroutine_hk(PVOID monoBehavior, PVOID param2) {
    InternalModule mainMod(true, true, OnInit);

    PVOID SetTimedLootActionRet = (PVOID)(0xA2037A + (DWORD64)sharedData.modData.pMainModuleBase);
    if (SetTimedLootActionRet == _ReturnAddress()) {
        *(float*)((DWORD64)param2 + 0x28) -= 0.2f;
    }
    fnStartCoroutine StartCoroutine = (fnStartCoroutine)InternalJTableManager::target(INTERNAL_JTABLE_TARGETS::StartCoroutine);
    return StartCoroutine(monoBehavior, param2);
}

/*
* Keep in mind logging is not initialized when DllMain is called
*/
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  dwReasonForCall,
                       LPVOID lpReserved
                     )
{
    switch (dwReasonForCall)
    {
    case DLL_PROCESS_ATTACH:
        break;
    }
    return TRUE;
}

