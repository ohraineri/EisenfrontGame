#include "infantry_entity.h"

#include "eisenfront/input.h"
#include "player_movement.h"
#include "player_tuning.h"

/* Defined further down, alongside the foot-contact queue's own
 * equivalent helper - forward-declared so resolve_vertical_state() can
 * enqueue a landing event at the point it detects one. */
static void infantry_landing_enqueue_event(InfantryLandingEventQueue *queue,
                                            const InfantryFallLandingEvent *event);

static void destroy_partial(World *world, CharacterController *controller) {
    if (controller != nullptr) {
        character_controller_destroy(controller);
    }
    if (world != nullptr) {
        world_destroy(world);
    }
}

Result infantry_entity_create(PhysicsWorld *physics_world, vec3 start_position, float aspect_ratio,
                               InfantryEntity *out_entity) {
    if (physics_world == nullptr || out_entity == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    WorldDesc world_desc = world_desc_default();
    world_desc.max_entities = 1u;
    World *world = nullptr;
    Result result = world_create(&world_desc, &world);
    if (result != RESULT_OK) {
        return result;
    }

    ComponentTypeId transform_type, locomotion_type, view_type;
    result = world_register_component_type(world, sizeof(InfantryTransformComponent), &transform_type);
    if (result != RESULT_OK) {
        destroy_partial(world, nullptr);
        return result;
    }
    result = world_register_component_type(world, sizeof(InfantryLocomotionComponent), &locomotion_type);
    if (result != RESULT_OK) {
        destroy_partial(world, nullptr);
        return result;
    }
    result = world_register_component_type(world, sizeof(InfantryViewComponent), &view_type);
    if (result != RESULT_OK) {
        destroy_partial(world, nullptr);
        return result;
    }

    Entity entity;
    result = entity_create(world, &entity);
    if (result != RESULT_OK) {
        destroy_partial(world, nullptr);
        return result;
    }

    const CharacterControllerDesc controller_desc = {
        .radius = player_movement_capsule_radius_for_stance(PLAYER_STANCE_STANDING),
        .half_height = player_movement_capsule_half_height_for_stance(PLAYER_STANCE_STANDING),
        .position = {start_position[0], start_position[1], start_position[2]},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    result = character_controller_create(physics_world, &controller_desc, &controller);
    if (result != RESULT_OK) {
        destroy_partial(world, nullptr);
        return result;
    }

    /* character_controller_is_grounded() defaults to false until the
     * first real probe - indistinguishable, to the vertical-state
     * machine below, from "just left the ground". A zero-displacement
     * move forces a real probe immediately, so the entity starts in
     * whatever vertical state actually matches where it spawned
     * (GROUNDED on solid footing, FALLING if spawned over open air)
     * instead of a phantom "was airborne" that would otherwise trigger
     * a bogus landing event and jump lockout on the very first tick. */
    vec3 settled_position;
    character_controller_move(controller, (vec3){0.0f, 0.0f, 0.0f}, 0.0f, settled_position);
    const bool spawned_grounded = character_controller_is_grounded(controller);

    const float eye_height_offset = player_movement_eye_height_offset_for_stance(PLAYER_STANCE_STANDING);

    const InfantryTransformComponent transform = {
        .position = {start_position[0], start_position[1], start_position[2]},
        .body_yaw_radians = 0.0f,
    };
    result = ecs_add_component(world, entity, transform_type, &transform);
    if (result != RESULT_OK) {
        destroy_partial(world, controller);
        return result;
    }

    const InfantryLocomotionComponent locomotion = {
        .controller = controller,
        .eye_height_offset = eye_height_offset,
        .noclip_base_speed_units_per_sec = PLAYER_NOCLIP_BASE_SPEED_UNITS_PER_SEC,
        .vertical_velocity = 0.0f,
        .horizontal_velocity = {0.0f, 0.0f, 0.0f},
        .stamina = PLAYER_STAMINA_MAX,
        .sprint_locked_out = false,
        .is_sprinting = false,
        .sprint_cone_exit_seconds = 0.0f,
        .last_moving_tier = PLAYER_SPEED_TIER_WALK,
        .stance = PLAYER_STANCE_STANDING,
        .stance_change_cooldown_seconds = 0.0f,
        .vertical_state =
            spawned_grounded ? PLAYER_VERTICAL_STATE_GROUNDED : PLAYER_VERTICAL_STATE_FALLING,
        .jump_lockout_seconds_remaining = 0.0f,
        .landing_recovery_seconds_remaining = 0.0f,
        .stand_to_jump_pending_seconds = 0.0f,
        .air_time_seconds = 0.0f,
        .fall_start_height = start_position[1],
        .max_downward_speed = 0.0f,
        .jump_initiated_current_airborne_phase = false,
        .last_landing_event = {0},
        .has_landing_event = false,
        .gait =
            {
                .stride = player_movement_gait_stride_state_initial(),
                .idle_seconds = 0.0f,
                .was_noclip_enabled = false,
                .queue_head = 0,
                .queue_count = 0,
                .dropped_event_count = 0,
                .dropped_contact_count = 0,
                .current_gait = {.stance = PLAYER_STANCE_STANDING, .speed_tier = PLAYER_SPEED_TIER_RUN},
                .motion_valid = false,
                .reset_generation = 1,
            },
        .landing_queue =
            {
                .queue_head = 0,
                .queue_count = 0,
                .dropped_event_count = 0,
            },
    };
    result = ecs_add_component(world, entity, locomotion_type, &locomotion);
    if (result != RESULT_OK) {
        destroy_partial(world, controller);
        return result;
    }

    vec3 eye_position = {start_position[0], start_position[1] + eye_height_offset, start_position[2]};
    const InfantryViewComponent view = {
        .camera = camera_create_perspective(eye_position, glm_rad(70.0f), aspect_ratio, 0.05f, 500.0f),
        .look_sensitivity_radians_per_pixel = 0.0025f,
        .noclip_enabled = false,
    };
    result = ecs_add_component(world, entity, view_type, &view);
    if (result != RESULT_OK) {
        destroy_partial(world, controller);
        return result;
    }

    out_entity->world = world;
    out_entity->entity = entity;
    out_entity->transform_type = transform_type;
    out_entity->locomotion_type = locomotion_type;
    out_entity->view_type = view_type;
    return RESULT_OK;
}

void infantry_entity_destroy(InfantryEntity *entity) {
    if (entity == nullptr || entity->world == nullptr) {
        return;
    }
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(entity);
    if (locomotion != nullptr) {
        character_controller_destroy(locomotion->controller);
    }
    world_destroy(entity->world);
    entity->world = nullptr;
}

InfantryTransformComponent *infantry_entity_get_transform(const InfantryEntity *entity) {
    return (InfantryTransformComponent *)ecs_get_component(entity->world, entity->entity,
                                                            entity->transform_type);
}

InfantryLocomotionComponent *infantry_entity_get_locomotion(const InfantryEntity *entity) {
    return (InfantryLocomotionComponent *)ecs_get_component(entity->world, entity->entity,
                                                             entity->locomotion_type);
}

InfantryViewComponent *infantry_entity_get_view(const InfantryEntity *entity) {
    return (InfantryViewComponent *)ecs_get_component(entity->world, entity->entity, entity->view_type);
}

/* Camera forward/right flattened to the XZ plane (pitch shouldn't tip
 * the player into walking into the floor/sky when looking up or down),
 * combined with the command's move axes into a unit wish direction. */
static void compute_wish_direction(const InfantryViewComponent *view, const InfantryInputCommand *command,
                                    vec3 out_wish_direction_unit, bool *out_has_move_input) {
    vec3 forward, right;
    camera_get_forward(&view->camera, forward);
    camera_get_right(&view->camera, right);
    forward[1] = 0.0f;
    right[1] = 0.0f;
    glm_vec3_normalize(forward);
    glm_vec3_normalize(right);

    vec3 wish = {0.0f, 0.0f, 0.0f};
    glm_vec3_muladds(forward, command->move_axis_y, wish);
    glm_vec3_muladds(right, command->move_axis_x, wish);

    const float wish_len = glm_vec3_norm(wish);
    *out_has_move_input = wish_len > 0.0f;
    if (*out_has_move_input) {
        glm_vec3_scale(wish, 1.0f / wish_len, wish);
    }
    glm_vec3_copy(wish, out_wish_direction_unit);
}

/* Tracks time outside the sprint cone (spec 7.1) and reports whether
 * sprint is contextually eligible right now (grounded, within the cone
 * or still inside its exit grace period, and not on a too-steep
 * slope). Airborne ends sprint immediately - no grace period for that
 * condition. */
static bool resolve_sprint_context(InfantryLocomotionComponent *locomotion, vec3 forward_flat,
                                    vec3 wish_direction_unit, bool has_move_input, bool grounded,
                                    float slope_angle_radians, float delta_seconds) {
    const bool within_cone =
        has_move_input &&
        player_movement_is_within_cone(forward_flat, wish_direction_unit, PLAYER_SPRINT_CONE_HALF_ANGLE_RADIANS);
    if (within_cone) {
        locomotion->sprint_cone_exit_seconds = 0.0f;
    } else {
        locomotion->sprint_cone_exit_seconds += delta_seconds;
    }
    return grounded && locomotion->stance == PLAYER_STANCE_STANDING &&
           locomotion->sprint_cone_exit_seconds <= PLAYER_SPRINT_CONE_GRACE_SECONDS &&
           slope_angle_radians <= PLAYER_SPRINT_DISABLE_SLOPE_ANGLE_RADIANS;
}

/* Resolves this tick's stance from crouch_held (spec Part IX). The
 * collision resize is always attempted atomically and instantly - the
 * cooldown only rate-limits how often a NEW change may be attempted
 * (spec 20.3's anti-spam), it never leaves the capsule half-resized.
 * A failed attempt (e.g. standing up under a low ceiling) sets no
 * cooldown, so it retries automatically every tick the intent to
 * stand persists (spec 20.2). Eye height blends toward the confirmed
 * stance's target independently of whether the resize succeeded this
 * tick.
 *
 * crouch_held is ignored entirely while airborne (!grounded) - a
 * deliberate judgment call for the crouch/jump interaction: there is
 * no ground reference to crouch relative to mid-air, and refusing to
 * resize at all while airborne is what makes mid-air capsule resizing
 * structurally impossible to use for dodging collision or incoming
 * fire, rather than merely discouraged. */
static void resolve_stance(InfantryLocomotionComponent *locomotion, bool crouch_held, float delta_seconds) {
    if (locomotion->stance_change_cooldown_seconds > 0.0f) {
        locomotion->stance_change_cooldown_seconds -= delta_seconds;
        if (locomotion->stance_change_cooldown_seconds < 0.0f) {
            locomotion->stance_change_cooldown_seconds = 0.0f;
        }
    }

    /* Gated on vertical_state, not the raw character_controller_is_grounded()
     * flag: that flag's hysteresis (physics.h) is meant to smooth a
     * missed probe on uneven ground, not to keep treating a player as
     * grounded for several ticks after a deliberate jump impulse - left
     * to that, crouch could sneak in during the hysteresis window right
     * after takeoff (crouch/jump interaction points 6/7). vertical_state
     * flips to JUMP_ASCENDING immediately on takeoff (see
     * try_fire_jump()), so it's the authoritative signal here. */
    const bool grounded_for_stance = locomotion->vertical_state == PLAYER_VERTICAL_STATE_GROUNDED ||
                                      locomotion->vertical_state == PLAYER_VERTICAL_STATE_LANDING_RECOVERY;
    if (grounded_for_stance) {
        const PlayerStance desired = crouch_held ? PLAYER_STANCE_CROUCHING : PLAYER_STANCE_STANDING;
        if (desired != locomotion->stance && locomotion->stance_change_cooldown_seconds <= 0.0f) {
            const float new_radius = player_movement_capsule_radius_for_stance(desired);
            const float new_half_height = player_movement_capsule_half_height_for_stance(desired);
            if (character_controller_resize(locomotion->controller, new_radius, new_half_height)) {
                locomotion->stance = desired;
                locomotion->stance_change_cooldown_seconds = PLAYER_STANCE_TRANSITION_SECONDS;
            }
        }
    }

    const float target_eye_height = player_movement_eye_height_offset_for_stance(locomotion->stance);
    locomotion->eye_height_offset = player_movement_approach_scalar(
        locomotion->eye_height_offset, target_eye_height,
        PLAYER_STANCE_EYE_HEIGHT_BLEND_RATE_METERS_PER_SEC, delta_seconds);
}

/* Drains/regenerates stamina and resolves this tick's speed tier. */
static PlayerSpeedTier resolve_sprint_and_stamina(InfantryLocomotionComponent *locomotion,
                                                   const InfantryInputCommand *command, bool has_move_input,
                                                   bool sprint_context_allowed, float delta_seconds) {
    StaminaState stamina_state = {
        .stamina = locomotion->stamina,
        .sprint_locked_out = locomotion->sprint_locked_out,
    };
    stamina_state = player_movement_update_stamina(stamina_state, command->sprint_held, has_move_input,
                                                    sprint_context_allowed, delta_seconds);
    locomotion->stamina = stamina_state.stamina;
    locomotion->sprint_locked_out = stamina_state.sprint_locked_out;

    const PlayerSpeedTier tier =
        player_movement_select_speed_tier(command->sprint_held, command->walk_modifier_held,
                                           locomotion->sprint_locked_out, has_move_input,
                                           sprint_context_allowed);
    locomotion->is_sprinting = (tier == PLAYER_SPEED_TIER_SPRINT);
    return tier;
}

/* Picks this tick's accel/decel constant: a small corrective-only cap
 * while airborne (spec 8's table), a reduced cap while recovering from
 * a landing (spec 19: "landing recovery may limit acceleration...but
 * must not completely remove camera control" - camera/look is never
 * gated, only this), the previous moving tier's braking constant while
 * coasting to a stop or mid-reversal (spec 9/10 - "state before
 * release"/"brake before accelerating opposite"), otherwise the
 * current tier's own acceleration. */
static float select_horizontal_accel(const InfantryLocomotionComponent *locomotion, bool grounded,
                                      bool has_move_input, PlayerTurnSeverity severity,
                                      PlayerSpeedTier effective_tier) {
    if (!grounded) {
        return PLAYER_AIRBORNE_ACCEL_METERS_PER_SEC2;
    }
    if (locomotion->vertical_state == PLAYER_VERTICAL_STATE_LANDING_RECOVERY) {
        return PLAYER_LANDING_RECOVERY_ACCEL_METERS_PER_SEC2;
    }
    if (!has_move_input) {
        return player_movement_decel_for_tier(locomotion->last_moving_tier);
    }
    if (severity == PLAYER_TURN_SEVERITY_REVERSAL) {
        return player_movement_decel_for_tier(effective_tier);
    }
    return player_movement_accel_for_tier(effective_tier);
}

/* Accelerates/decelerates locomotion->horizontal_velocity toward the
 * speed implied by wish_direction_unit + tier, applying spec 10's
 * turn-severity speed scaling, spec 10.2's sprint curvature clamp
 * (which may demote a too-sharp sprint turn to Run for this tick), and
 * spec 14's uphill slope penalty (see player_tuning.h's file header
 * comment on why this is currently always a no-op multiplier of 1.0 in
 * this scene). */
static void integrate_horizontal_velocity(InfantryLocomotionComponent *locomotion, vec3 wish_direction_unit,
                                           bool has_move_input, PlayerSpeedTier tier, bool grounded,
                                           vec3 ground_normal, float delta_seconds) {
    PlayerSpeedTier effective_tier = tier;
    vec3            effective_direction;
    glm_vec3_copy(wish_direction_unit, effective_direction);

    if (has_move_input && tier == PLAYER_SPEED_TIER_SPRINT) {
        vec3       clamped_direction;
        const bool exceeded = player_movement_clamp_sprint_turn(
            locomotion->horizontal_velocity, wish_direction_unit,
            player_movement_speed_for_tier(PLAYER_SPEED_TIER_SPRINT), delta_seconds, clamped_direction);
        glm_vec3_copy(clamped_direction, effective_direction);
        if (exceeded) {
            effective_tier = PLAYER_SPEED_TIER_RUN;
        }
    }

    const PlayerTurnSeverity severity = has_move_input
        ? player_movement_classify_turn(locomotion->horizontal_velocity, effective_direction)
        : PLAYER_TURN_SEVERITY_MINOR;
    const float turn_speed_multiplier = player_movement_turn_speed_multiplier(severity);

    const float slope_angle = player_movement_slope_angle_radians(ground_normal);
    const float uphill_component = player_movement_uphill_component(ground_normal, effective_direction);
    const float slope_speed_multiplier =
        player_movement_slope_speed_multiplier(effective_tier, slope_angle, uphill_component);

    vec3 target_velocity;
    player_movement_target_velocity(effective_direction,
                                     player_movement_speed_for_tier(effective_tier) *
                                         turn_speed_multiplier * slope_speed_multiplier,
                                     target_velocity);

    const float accel = select_horizontal_accel(locomotion, grounded, has_move_input, severity, effective_tier);
    player_movement_approach_velocity(locomotion->horizontal_velocity, target_velocity, accel, delta_seconds,
                                       locomotion->horizontal_velocity);

    if (has_move_input) {
        locomotion->last_moving_tier = effective_tier;
    }
}

/* Applies the takeoff impulse if (and only if) every jump eligibility
 * check passes (spec 17): grounded, standing (crouch/jump interaction
 * point 1 - never fires while the crouched capsule is what's in use),
 * not still locked out from a previous landing, and enough stamina
 * (spec 17's "data-driven" jump stamina cost - also one of spec
 * 17.3's bunny-hop deterrents alongside the lockout below). Silently
 * does nothing on failure - jump_requested was already a one-shot
 * edge with nothing left to retry against. */
static bool try_fire_jump(InfantryLocomotionComponent *locomotion, bool grounded, float current_height) {
    if (!grounded || locomotion->stance != PLAYER_STANCE_STANDING ||
        locomotion->jump_lockout_seconds_remaining > 0.0f ||
        locomotion->stamina < PLAYER_JUMP_STAMINA_COST) {
        return false;
    }
    locomotion->vertical_velocity = PLAYER_JUMP_SPEED_METERS_PER_SEC;
    locomotion->stamina -= PLAYER_JUMP_STAMINA_COST;
    /* Force the airborne transition immediately rather than waiting for
     * character_controller_is_grounded() to catch up: its hysteresis
     * (physics.h) exists to smooth over a missed probe on uneven
     * ground, not to keep treating a player as grounded for several
     * ticks after a deliberate jump impulse - left to that, crouch
     * could still sneak in during the hysteresis window (crouch/jump
     * interaction points 6/7). vertical_state is the authoritative
     * signal everything else (resolve_stance included) checks instead. */
    locomotion->vertical_state = PLAYER_VERTICAL_STATE_JUMP_ASCENDING;
    locomotion->jump_initiated_current_airborne_phase = true;
    locomotion->fall_start_height = current_height;
    locomotion->air_time_seconds = 0.0f;
    locomotion->max_downward_speed = 0.0f;
    return true;
}

/* Handles a jump request (spec 17 + the crouch/jump interaction):
 * - Standing: fires immediately through the normal eligibility checks.
 * - Crouching: never jumps in the same tick, even if standing
 *   succeeds (crouch/jump point 5 - no combined instant-expand-and-
 *   jump exploit). Attempts to stand up right now; if blocked (a low
 *   ceiling), the jump is rejected outright (point 3) and nothing is
 *   queued. If the stand succeeds, the actual jump is deferred by
 *   PLAYER_STANCE_TRANSITION_SECONDS - the same "approved" duration
 *   spec Part IX already uses for a stance transition (point 4) -
 *   after which it fires if the entity is still grounded and standing.
 * Returns true if a jump impulse was actually applied THIS tick. */
static bool resolve_jump_request(InfantryLocomotionComponent *locomotion, bool jump_requested,
                                  bool grounded, float current_height) {
    if (jump_requested && grounded) {
        if (locomotion->stance == PLAYER_STANCE_CROUCHING) {
            const float standing_radius = player_movement_capsule_radius_for_stance(PLAYER_STANCE_STANDING);
            const float standing_half_height =
                player_movement_capsule_half_height_for_stance(PLAYER_STANCE_STANDING);
            if (character_controller_resize(locomotion->controller, standing_radius, standing_half_height)) {
                locomotion->stance = PLAYER_STANCE_STANDING;
                locomotion->stance_change_cooldown_seconds = PLAYER_STANCE_TRANSITION_SECONDS;
                locomotion->stand_to_jump_pending_seconds = PLAYER_STANCE_TRANSITION_SECONDS;
            }
            /* else: standing clearance blocked - jump rejected, nothing queued. */
        } else {
            return try_fire_jump(locomotion, grounded, current_height);
        }
    }
    return false;
}

/* Detects landing (an airborne->grounded transition), classifies its
 * severity, starts full-control recovery and the repeat-jump lockout
 * (spec 19/17.3), and populates the FFallLandingEvent stub. Then
 * advances air-time/apex tracking while airborne, and finally
 * integrates gravity - unconditionally, exactly like before this
 * milestone. Returns true exactly on the tick a landing is detected
 * (used by the M10 gait logic to keep landing a separate event from an
 * ordinary foot-contact - see update_gait()). */
static bool resolve_vertical_state(InfantryLocomotionComponent *locomotion, bool grounded,
                                    float current_height, vec3 ground_normal, float delta_seconds) {
    if (locomotion->jump_lockout_seconds_remaining > 0.0f) {
        locomotion->jump_lockout_seconds_remaining -= delta_seconds;
        if (locomotion->jump_lockout_seconds_remaining < 0.0f) {
            locomotion->jump_lockout_seconds_remaining = 0.0f;
        }
    }

    if (grounded) {
        /* Grounded reading TRUE while still JUMP_ASCENDING isn't
         * necessarily hysteresis or a bug: the ground probe has a
         * fixed reach (a fraction of capsule radius, see physics.c),
         * and a modest jump's first tick or two of vertical travel can
         * still be within that reach for real. A landing can't
         * physically happen while still moving upward, so it's only
         * treated as one once vertical_velocity has actually turned
         * non-positive (at or past apex). */
        bool       landed_this_tick = false;
        const bool was_airborne = (locomotion->vertical_state == PLAYER_VERTICAL_STATE_JUMP_ASCENDING ||
                                    locomotion->vertical_state == PLAYER_VERTICAL_STATE_FALLING) &&
                                   locomotion->vertical_velocity <= 0.0f;
        if (was_airborne) {
            const float                 impact_speed = fabsf(locomotion->vertical_velocity);
            const PlayerLandingSeverity severity = player_movement_classify_landing(impact_speed);
            locomotion->last_landing_event = (InfantryFallLandingEvent){
                .fall_start_height = locomotion->fall_start_height,
                .landing_height = current_height,
                .max_downward_speed = locomotion->max_downward_speed,
                .impact_speed = impact_speed,
                .air_time_seconds = locomotion->air_time_seconds,
                .surface_normal = {ground_normal[0], ground_normal[1], ground_normal[2]},
                .was_jump_initiated = locomotion->jump_initiated_current_airborne_phase,
                .severity = severity,
            };
            locomotion->has_landing_event = true;
            /* Same event, dispatched through the bounded queue too (M11) -
             * the sticky fields above stay a separate "last known landing"
             * readout (debug_overlay.c), this is the single-dispatcher
             * path a presentation consumer (camera_motion.c) drains. */
            infantry_landing_enqueue_event(&locomotion->landing_queue, &locomotion->last_landing_event);
            locomotion->landing_recovery_seconds_remaining = player_movement_landing_recovery_seconds(severity);
            locomotion->jump_lockout_seconds_remaining = PLAYER_JUMP_LOCKOUT_SECONDS;
            locomotion->vertical_state = PLAYER_VERTICAL_STATE_LANDING_RECOVERY;
            landed_this_tick = true;
        } else if (locomotion->vertical_state == PLAYER_VERTICAL_STATE_LANDING_RECOVERY) {
            locomotion->landing_recovery_seconds_remaining -= delta_seconds;
            if (locomotion->landing_recovery_seconds_remaining <= 0.0f) {
                locomotion->landing_recovery_seconds_remaining = 0.0f;
                locomotion->vertical_state = PLAYER_VERTICAL_STATE_GROUNDED;
            }
        }
        if (landed_this_tick) {
            return true;
        }
    } else {
        const bool leaving_ground = locomotion->vertical_state == PLAYER_VERTICAL_STATE_GROUNDED ||
                                     locomotion->vertical_state == PLAYER_VERTICAL_STATE_LANDING_RECOVERY;
        if (leaving_ground) {
            /* vertical_velocity > 0 here means a jump impulse was just
             * applied this same tick (resolve_jump_request() runs
             * before this function) - anything else is walking off a
             * ledge. */
            const bool jumped = locomotion->vertical_velocity > 0.0f;
            locomotion->vertical_state = jumped ? PLAYER_VERTICAL_STATE_JUMP_ASCENDING
                                                 : PLAYER_VERTICAL_STATE_FALLING;
            locomotion->jump_initiated_current_airborne_phase = jumped;
            locomotion->fall_start_height = current_height;
            locomotion->air_time_seconds = 0.0f;
            locomotion->max_downward_speed = 0.0f;
        }
        locomotion->air_time_seconds += delta_seconds;
        if (-locomotion->vertical_velocity > locomotion->max_downward_speed) {
            locomotion->max_downward_speed = -locomotion->vertical_velocity;
        }
        if (locomotion->vertical_state == PLAYER_VERTICAL_STATE_JUMP_ASCENDING &&
            locomotion->vertical_velocity <= 0.0f) {
            locomotion->vertical_state = PLAYER_VERTICAL_STATE_FALLING;
        }
    }
    return false;
}

/* Gravity, jump, and the small grounded-stick that keeps the ground
 * probe from flickering right at zero vertical velocity. Returns
 * whether a landing was detected this tick (propagated from
 * resolve_vertical_state()). */
static bool integrate_vertical_velocity(InfantryLocomotionComponent *locomotion,
                                         const InfantryInputCommand *command, float current_height,
                                         vec3 ground_normal, float delta_seconds) {
    const bool grounded = character_controller_is_grounded(locomotion->controller);

    const bool landed_this_tick =
        resolve_vertical_state(locomotion, grounded, current_height, ground_normal, delta_seconds);

    bool jumped_this_tick = false;
    if (grounded) {
        jumped_this_tick =
            resolve_jump_request(locomotion, command->jump_requested, grounded, current_height);
        if (!jumped_this_tick) {
            if (locomotion->stand_to_jump_pending_seconds > 0.0f) {
                locomotion->stand_to_jump_pending_seconds -= delta_seconds;
                if (locomotion->stand_to_jump_pending_seconds < 0.0f) {
                    locomotion->stand_to_jump_pending_seconds = 0.0f;
                }
                if (locomotion->stand_to_jump_pending_seconds <= 0.0f &&
                    locomotion->stance == PLAYER_STANCE_STANDING) {
                    jumped_this_tick = try_fire_jump(locomotion, grounded, current_height);
                }
            }
            if (!jumped_this_tick && locomotion->vertical_velocity < 0.0f) {
                locomotion->vertical_velocity = PLAYER_GROUNDED_STICK_VELOCITY_UNITS_PER_SEC;
            }
        }
    }
    /* A jump firing this tick forces vertical_state to JUMP_ASCENDING
     * immediately (see try_fire_jump()) - not left for
     * resolve_vertical_state() to infer next tick from a
     * character_controller_is_grounded() that hysteresis can keep
     * reporting true for a while yet. vertical_velocity is left
     * exactly as try_fire_jump() set it, not touched by the
     * grounded-stick branch above. */

    locomotion->vertical_velocity += PLAYER_GRAVITY_UNITS_PER_SEC2 * delta_seconds;
    return landed_this_tick;
}

/* Concatenates horizontal + vertical displacement, resolves collision
 * via the CharacterController, and writes the result to transform +
 * camera. */
static void apply_movement(InfantryTransformComponent *transform, InfantryLocomotionComponent *locomotion,
                            InfantryViewComponent *view, float delta_seconds) {
    vec3 desired_displacement = {
        locomotion->horizontal_velocity[0] * delta_seconds,
        locomotion->vertical_velocity * delta_seconds,
        locomotion->horizontal_velocity[2] * delta_seconds,
    };
    vec3 resulting_position;
    character_controller_move(locomotion->controller, desired_displacement, delta_seconds,
                               resulting_position);

    glm_vec3_copy(resulting_position, transform->position);

    view->camera.position[0] = resulting_position[0];
    view->camera.position[1] = resulting_position[1] + locomotion->eye_height_offset;
    view->camera.position[2] = resulting_position[2];
}

/* Appends to the bounded FIFO, dropping the OLDEST unconsumed event on
 * overflow (production must never block waiting for the dispatcher) -
 * see infantry_entity_pop_foot_contact_event()'s doc comment for the
 * single-dispatcher contract this queue exists to support. */
static void infantry_gait_enqueue_event(InfantryGaitState *state, const InfantryFootContactEvent *event) {
    if (state->queue_count >= INFANTRY_FOOT_CONTACT_EVENT_CAPACITY) {
        state->queue_head = (state->queue_head + 1u) % INFANTRY_FOOT_CONTACT_EVENT_CAPACITY;
        state->queue_count -= 1u;
        state->dropped_event_count += 1u;
    }
    const uint32_t tail = (state->queue_head + state->queue_count) % INFANTRY_FOOT_CONTACT_EVENT_CAPACITY;
    state->queue[tail] = *event;
    state->queue_count += 1u;
}

/* Same overflow policy as infantry_gait_enqueue_event() above, for
 * InfantryFallLandingEvent (M11's landing camera response). */
static void infantry_landing_enqueue_event(InfantryLandingEventQueue *queue,
                                            const InfantryFallLandingEvent *event) {
    if (queue->queue_count >= INFANTRY_LANDING_EVENT_QUEUE_CAPACITY) {
        queue->queue_head = (queue->queue_head + 1u) % INFANTRY_LANDING_EVENT_QUEUE_CAPACITY;
        queue->queue_count -= 1u;
        queue->dropped_event_count += 1u;
    }
    const uint32_t tail = (queue->queue_head + queue->queue_count) % INFANTRY_LANDING_EVENT_QUEUE_CAPACITY;
    queue->queue[tail] = *event;
    queue->queue_count += 1u;
}

/* Turns this tick's ACTUAL realized grounded planar displacement (the
 * post-collide-and-slide result apply_movement() already produced -
 * never requested input or raw velocity) into zero or more queued
 * InfantryFootContactEvents (M10). Gating, in order:
 *   - a landing this tick resets gait and produces nothing (landing is
 *     InfantryFallLandingEvent's event, not an ordinary foot contact);
 *   - airborne ticks are never fed displacement at all;
 *   - below-threshold planar speed accumulates toward the documented
 *     idle-reset (crouching/standing still never produces a contact,
 *     since real displacement is ~0 regardless).
 * Surface is resolved only for an event actually about to be created,
 * at that event's own world_position - never every tick, never a
 * stale/camera position. */
static void update_gait(InfantryLocomotionComponent *locomotion, vec3 position_before_move,
                         vec3 position_after_move, bool was_grounded_this_tick, bool landed_this_tick,
                         PlayerGait gait, float planar_speed, float delta_seconds, uint64_t sim_tick_index,
                         InfantryGroundSurfaceLookupFn surface_lookup_fn, void *surface_lookup_userdata) {
    InfantryGaitState *state = &locomotion->gait;
    /* Persisted every tick regardless of branch below, so
     * infantry_entity_get_gait_presentation_snapshot() always reflects
     * the gait that was actually resolved this tick, even on ticks that
     * produce no stride accumulation at all (airborne/idle). */
    state->current_gait = gait;

    if (landed_this_tick) {
        player_movement_gait_stride_state_reset(&state->stride);
        state->idle_seconds = 0.0f;
        state->motion_valid = false;
        return;
    }
    if (!was_grounded_this_tick) {
        state->motion_valid = false;
        return;
    }

    if (planar_speed < PLAYER_GAIT_MIN_PLANAR_SPEED_METERS_PER_SEC) {
        state->idle_seconds += delta_seconds;
        if (state->idle_seconds >= PLAYER_GAIT_IDLE_RESET_SECONDS) {
            player_movement_gait_stride_state_reset(&state->stride);
        }
        state->motion_valid = false;
        return;
    }
    state->idle_seconds = 0.0f;
    state->motion_valid = true;

    vec3 delta;
    glm_vec3_sub(position_after_move, position_before_move, delta);
    const float planar_displacement = sqrtf(delta[0] * delta[0] + delta[2] * delta[2]);

    PlayerFoot     feet[PLAYER_MAX_FOOT_CONTACTS_PER_TICK];
    uint32_t       dropped_whole_strides = 0;
    const float    stride_length = player_movement_stride_length_for_gait(gait);
    const uint32_t emitted = player_movement_gait_stride_advance(
        &state->stride, planar_displacement, stride_length, PLAYER_MAX_FOOT_CONTACTS_PER_TICK, feet,
        PLAYER_MAX_FOOT_CONTACTS_PER_TICK, &dropped_whole_strides);
    state->dropped_contact_count += dropped_whole_strides;

    const float intensity = player_movement_footstep_intensity_for_gait(gait);
    for (uint32_t i = 0; i < emitted; ++i) {
        InfantryFootContactEvent event = {
            .foot = feet[i],
            .surface_type = surface_lookup_fn != nullptr
                                ? surface_lookup_fn(surface_lookup_userdata, position_after_move)
                                : SURFACE_TYPE_SOIL,
            .gait = gait,
            .movement_speed = planar_speed,
            .intensity = intensity,
            .sim_tick_index = sim_tick_index,
            .grounded = true,
        };
        glm_vec3_copy(position_after_move, event.world_position);
        infantry_gait_enqueue_event(state, &event);
    }
}

static void update_grounded(InfantryTransformComponent *transform, InfantryLocomotionComponent *locomotion,
                             InfantryViewComponent *view, const InfantryInputCommand *command,
                             float delta_seconds, uint64_t sim_tick_index,
                             InfantryGroundSurfaceLookupFn surface_lookup_fn,
                             void *surface_lookup_userdata) {
    vec3 position_before_move;
    glm_vec3_copy(transform->position, position_before_move);

    vec3 wish_direction_unit;
    bool has_move_input;
    compute_wish_direction(view, command, wish_direction_unit, &has_move_input);

    const float movement_yaw = player_movement_yaw_from_direction(wish_direction_unit);
    transform->body_yaw_radians = player_movement_resolve_body_yaw(
        transform->body_yaw_radians, view->camera.yaw_radians, movement_yaw, has_move_input,
        PLAYER_TURN_IN_PLACE_THRESHOLD_RADIANS);

    const bool grounded = character_controller_is_grounded(locomotion->controller);
    vec3       ground_normal;
    character_controller_get_ground_normal(locomotion->controller, ground_normal);
    const float slope_angle = player_movement_slope_angle_radians(ground_normal);

    resolve_stance(locomotion, command->crouch_held, delta_seconds);

    vec3 forward_flat;
    camera_get_forward(&view->camera, forward_flat);
    forward_flat[1] = 0.0f;
    glm_vec3_normalize(forward_flat);

    const bool sprint_context_allowed = resolve_sprint_context(
        locomotion, forward_flat, wish_direction_unit, has_move_input, grounded, slope_angle, delta_seconds);

    PlayerSpeedTier tier = resolve_sprint_and_stamina(locomotion, command, has_move_input,
                                                       sprint_context_allowed, delta_seconds);
    if (locomotion->stance == PLAYER_STANCE_CROUCHING) {
        /* CrouchMove (spec 6) is a single-speed locomotion state - it
         * doesn't cross-multiply with sprint/walk-modifier. */
        tier = PLAYER_SPEED_TIER_CROUCH;
    }
    const PlayerGait gait = {.stance = locomotion->stance, .speed_tier = tier};

    integrate_horizontal_velocity(locomotion, wish_direction_unit, has_move_input, tier, grounded,
                                   ground_normal, delta_seconds);
    const bool landed_this_tick = integrate_vertical_velocity(locomotion, command, transform->position[1],
                                                                ground_normal, delta_seconds);
    const float planar_speed = glm_vec3_norm(locomotion->horizontal_velocity);
    apply_movement(transform, locomotion, view, delta_seconds);

    update_gait(locomotion, position_before_move, transform->position, grounded, landed_this_tick, gait,
                planar_speed, delta_seconds, sim_tick_index, surface_lookup_fn, surface_lookup_userdata);
}

static void update_noclip(InfantryTransformComponent *transform, InfantryLocomotionComponent *locomotion,
                           InfantryViewComponent *view, float delta_seconds) {
    vec3 forward, right, up;
    camera_get_forward(&view->camera, forward);
    camera_get_right(&view->camera, right);
    camera_get_up(&view->camera, up);

    const float speed = locomotion->noclip_base_speed_units_per_sec *
                         (input_key_held(KEY_LSHIFT) ? PLAYER_NOCLIP_SPEED_MULTIPLIER : 1.0f) *
                         delta_seconds;
    vec3 move = {0.0f, 0.0f, 0.0f};
    if (input_key_held(KEY_W)) {
        glm_vec3_muladds(forward, speed, move);
    }
    if (input_key_held(KEY_S)) {
        glm_vec3_muladds(forward, -speed, move);
    }
    if (input_key_held(KEY_D)) {
        glm_vec3_muladds(right, speed, move);
    }
    if (input_key_held(KEY_A)) {
        glm_vec3_muladds(right, -speed, move);
    }
    if (input_key_held(KEY_SPACE)) {
        glm_vec3_muladds(up, speed, move);
    }
    if (input_key_held(KEY_LCTRL)) {
        glm_vec3_muladds(up, -speed, move);
    }

    locomotion->vertical_velocity = 0.0f; /* no gravity accumulation while flying */
    glm_vec3_add(view->camera.position, move, view->camera.position);

    /* Keeps the controller (and transform) from drifting far from the
     * camera, so toggling noclip back off resumes grounded movement
     * from wherever the player actually flew to, not a stale position. */
    vec3 controller_position = {view->camera.position[0],
                                 view->camera.position[1] - locomotion->eye_height_offset,
                                 view->camera.position[2]};
    glm_vec3_copy(controller_position, transform->position);
    character_controller_set_position(locomotion->controller, controller_position);
}

void infantry_entity_apply_look(InfantryEntity *entity, const InfantryInputCommand *command) {
    InfantryViewComponent *view = infantry_entity_get_view(entity);
    camera_rotate(&view->camera, command->look_delta_yaw * view->look_sensitivity_radians_per_pixel,
                   -command->look_delta_pitch * view->look_sensitivity_radians_per_pixel);
}

void infantry_entity_update_fixed(InfantryEntity *entity, const InfantryInputCommand *command,
                                   float fixed_delta_seconds, uint64_t sim_tick_index,
                                   InfantryGroundSurfaceLookupFn surface_lookup_fn,
                                   void *surface_lookup_userdata) {
    InfantryTransformComponent  *transform = infantry_entity_get_transform(entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(entity);
    InfantryViewComponent       *view = infantry_entity_get_view(entity);

    /* Toggling noclip either direction is a scripted/free-fly position
     * change, exactly like a teleport - resets gait so a stale partial
     * stride from before the toggle can never surface as a phantom
     * footstep once grounded movement resumes. */
    if (view->noclip_enabled != locomotion->gait.was_noclip_enabled) {
        infantry_entity_reset_gait(entity);
        locomotion->gait.was_noclip_enabled = view->noclip_enabled;
    }

    if (view->noclip_enabled) {
        update_noclip(transform, locomotion, view, fixed_delta_seconds);
    } else {
        update_grounded(transform, locomotion, view, command, fixed_delta_seconds, sim_tick_index,
                         surface_lookup_fn, surface_lookup_userdata);
    }
}

bool infantry_entity_pop_foot_contact_event(InfantryEntity *entity, InfantryFootContactEvent *out_event) {
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(entity);
    InfantryGaitState           *state = &locomotion->gait;
    if (state->queue_count == 0u) {
        return false;
    }
    *out_event = state->queue[state->queue_head];
    state->queue_head = (state->queue_head + 1u) % INFANTRY_FOOT_CONTACT_EVENT_CAPACITY;
    state->queue_count -= 1u;
    return true;
}

void infantry_entity_reset_gait(InfantryEntity *entity) {
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(entity);
    player_movement_gait_stride_state_reset(&locomotion->gait.stride);
    locomotion->gait.idle_seconds = 0.0f;
    locomotion->gait.motion_valid = false;
    locomotion->gait.reset_generation += 1u;
}

void infantry_entity_get_gait_presentation_snapshot(const InfantryEntity *entity,
                                                      PlayerGaitPresentationSnapshot *out_snapshot) {
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(entity);
    const InfantryGaitState     *state = &locomotion->gait;
    const float stride_length = player_movement_stride_length_for_gait(state->current_gait);
    out_snapshot->foot_phase = state->stride.next_foot;
    out_snapshot->stride_progress01 = player_movement_gait_stride_progress01(state->stride, stride_length);
    out_snapshot->stride_length_meters = stride_length;
    out_snapshot->gait = state->current_gait;
    out_snapshot->motion_valid = state->motion_valid;
    out_snapshot->reset_generation = state->reset_generation;
}

bool infantry_entity_pop_landing_event(InfantryEntity *entity, InfantryFallLandingEvent *out_event) {
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(entity);
    InfantryLandingEventQueue   *queue = &locomotion->landing_queue;
    if (queue->queue_count == 0u) {
        return false;
    }
    *out_event = queue->queue[queue->queue_head];
    queue->queue_head = (queue->queue_head + 1u) % INFANTRY_LANDING_EVENT_QUEUE_CAPACITY;
    queue->queue_count -= 1u;
    return true;
}
