// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef RADDBG_LAYER_COLOR
#define RADDBG_LAYER_COLOR 0.80f, 0.40f, 0.35f

////////////////////////////////
//~ rjf: Globals

thread_static UI_State *ui_state = 0;

////////////////////////////////
//~ rjf: Basic Type Functions

internal U64
ui_hash_from_string(U64 seed, String8 string)
{
  U64 result = seed;
  for(U64 i = 0; i < string.size; i += 1)
  {
    result = ((result << 5) + result) + string.str[i];
  }
  return result;
}

internal String8
ui_hash_part_from_key_string(String8 string)
{
  String8 result = string;
  
  // rjf: look for ### patterns, which can replace the entirety of the part of
  // the string that is hashed.
  U64 hash_replace_signifier_pos = str8_find_needle(string, 0, str8_lit("###"), 0);
  if(hash_replace_signifier_pos < string.size)
  {
    result = str8_skip(string, hash_replace_signifier_pos);
  }
  
  return result;
}

internal String8
ui_display_part_from_key_string(String8 string)
{
  U64 hash_pos = str8_find_needle(string, 0, str8_lit("##"), 0);
  string.size = hash_pos;
  return string;
}

internal UI_Key
ui_key_zero(void)
{
  UI_Key result = {0};
  return result;
}

internal UI_Key
ui_key_make(U64 v)
{
  UI_Key result = {v};
  return result;
}

internal UI_Key
ui_key_from_string(UI_Key seed_key, String8 string)
{
  ProfBeginFunction();
  UI_Key result = {0};
  if(string.size != 0)
  {
    String8 hash_part = ui_hash_part_from_key_string(string);
    result.u64[0] = ui_hash_from_string(seed_key.u64[0], hash_part);
  }
  ProfEnd();
  return result;
}

internal UI_Key
ui_key_from_stringf(UI_Key seed_key, char *fmt, ...)
{
  Temp scratch = scratch_begin(0, 0);
  va_list args;
  va_start(args, fmt);
  String8 string = push_str8fv(scratch.arena, fmt, args);
  va_end(args);
  UI_Key key = ui_key_from_string(seed_key, string);
  scratch_end(scratch);
  return key;
}

internal B32
ui_key_match(UI_Key a, UI_Key b)
{
  return a.u64[0] == b.u64[0];
}

////////////////////////////////
//~ rjf: Navigation Action List Building & Consumption Functions

internal void
ui_nav_action_list_push(Arena *arena, UI_NavActionList *list, UI_NavAction action)
{
  UI_NavActionNode *node = push_array(arena, UI_NavActionNode, 1);
  DLLPushBack(list->first, list->last, node);
  MemoryCopyStruct(&node->v, &action);
  list->count += 1;
}

internal void
ui_nav_eat_action_node(UI_NavActionList *list, UI_NavActionNode *node)
{
  DLLRemove(list->first, list->last, node);
  list->count -= 1;
}

////////////////////////////////
//~ rjf: High Level Navigation Action => Text Operations
internal B32
ui_nav_char_is_code_symbol(U8 c)
{
  return (char_is_alpha(c) || char_is_digit(c, 10) || c == '_');
}

internal S64
ui_nav_scanned_column_from_column(String8 string, S64 start_column, Side side)
{
  S64 new_column = start_column;
  S64 delta = (!!side)*2 - 1;
  B32 found_text = 0;
  B32 found_non_space = 0;
  S64 start_off = delta < 0 ? delta : 0;
  for(S64 col = start_column+start_off; 1 <= col && col <= string.size+1; col += delta)
  {
    U8 byte = (col <= string.size) ? string.str[col-1] : 0;
    B32 is_non_space = !char_is_space(byte);
    B32 is_name = ui_nav_char_is_code_symbol(byte);
    
    if (((side == Side_Min) && (col == 1)) || 
        ((side == Side_Max) && (col == string.size+1)) ||
        (found_non_space && !is_non_space) || 
        (found_text && !is_name))
    {
      new_column = col + (!side && col != 1);  
      break;
    } else if (!found_text && is_name) {
      found_text = 1;
    } else if (!found_non_space && is_non_space ) {
      found_non_space = 1;
    }
  }
  return new_column;
}

internal UI_NavTxtOp
ui_nav_single_line_txt_op_from_action(Arena *arena, UI_NavAction action, String8 line, TxtPt cursor, TxtPt mark)
{
  TxtPt next_cursor = cursor;
  TxtPt next_mark = mark;
  TxtRng range = {0};
  String8 replace = {0};
  String8 copy = {0};
  UI_NavTxtOpFlags flags = 0;
  Vec2S32 delta = action.delta;
  Vec2S32 original_delta = delta;
  
  //- rjf: resolve high-level delta into byte delta, based on unit
  switch(action.delta_unit)
  {
    default:{}break;
    case UI_NavDeltaUnit_Element:
    {
      // TODO(rjf): this should account for multi-byte characters in UTF-8... for now, just assume ASCII and
      // no-op
    }break;
    case UI_NavDeltaUnit_Chunk:
    {
      delta.x = (S32)ui_nav_scanned_column_from_column(line, cursor.column, delta.x > 0 ? Side_Max : Side_Min) - cursor.column;
    }break;
    case UI_NavDeltaUnit_Whole:
    case UI_NavDeltaUnit_EndPoint:
    {
      S64 first_nonwhitespace_column = 1;
      for(U64 idx = 0; idx < line.size; idx += 1)
      {
        if(!char_is_space(line.str[idx]))
        {
          first_nonwhitespace_column = (S64)idx + 1;
          break;
        }
      }
      S64 home_dest_column = (cursor.column == first_nonwhitespace_column) ? 1 : first_nonwhitespace_column;
      delta.x = (delta.x > 0) ? ((S64)line.size+1 - cursor.column) : (home_dest_column - cursor.column);
    }break;
  }
  
  //- rjf: zero delta
  if(!txt_pt_match(cursor, mark) && action.flags & UI_NavActionFlag_ZeroDeltaOnSelect)
  {
    delta = v2s32(0, 0);
  }
  
  //- rjf: form next cursor
  if(txt_pt_match(cursor, mark) || !(action.flags & UI_NavActionFlag_ZeroDeltaOnSelect))
  {
    next_cursor.column += delta.x;
  }
  
  //- rjf: cap at line
  if(action.flags & UI_NavActionFlag_CapAtLine)
  {
    next_cursor.column = Clamp(1, next_cursor.column, (S64)(line.size+1));
  }
  
  //- rjf: in some cases, we want to pick a selection side based on the delta
  if(!txt_pt_match(cursor, mark) && action.flags & UI_NavActionFlag_PickSelectSide)
  {
    if(original_delta.x < 0 || original_delta.y < 0)
    {
      next_cursor = next_mark = txt_pt_min(cursor, mark);
    }
    else if(original_delta.x > 0 || original_delta.y > 0)
    {
      next_cursor = next_mark = txt_pt_max(cursor, mark);
    }
  }
  
  //- rjf: copying
  if(action.flags & UI_NavActionFlag_Copy)
  {
    if(cursor.line == mark.line)
    {
      copy = str8_substr(line, r1u64(cursor.column-1, mark.column-1));
      flags |= UI_NavTxtOpFlag_Copy;
    }
    else
    {
      flags |= UI_NavTxtOpFlag_Invalid;
    }
  }
  
  //- rjf: pasting
  if(action.flags & UI_NavActionFlag_Paste)
  {
    range = txt_rng(cursor, mark);
    replace = os_get_clipboard_text(arena);
    next_cursor = next_mark = txt_pt(cursor.line, cursor.column+replace.size);
  }
  
  //- rjf: deletion
  if(action.flags & UI_NavActionFlag_Delete)
  {
    TxtPt new_pos = txt_pt_min(next_cursor, next_mark);
    range = txt_rng(next_cursor, next_mark);
    replace = str8_lit("");
    next_cursor = next_mark = new_pos;
  }
  
  //- rjf: stick mark to cursor, when we don't want to keep it in the same spot
  if(!(action.flags & UI_NavActionFlag_KeepMark))
  {
    next_mark = next_cursor;
  }
  
  //- rjf: insertion
  if(action.insertion.size != 0)
  {
    range = txt_rng(cursor, mark);
    replace = push_str8_copy(arena, action.insertion);
    next_cursor = next_mark = txt_pt(range.min.line, range.min.column + action.insertion.size);
  }
  
  //- rjf: replace & commit -> replace entire range
  if(action.flags & UI_NavActionFlag_ReplaceAndCommit)
  {
    range = txt_rng(txt_pt(cursor.line, 1), txt_pt(cursor.line, line.size+1));
  }
  
  //- rjf: determine if this event should be taken, based on bounds of cursor
  {
    if(next_cursor.column > line.size+1 || 1 > next_cursor.column || action.delta.y != 0)
    {
      flags |= UI_NavTxtOpFlag_Invalid;
    }
    next_cursor.column = Clamp(1, next_cursor.column, line.size+replace.size+1);
    next_mark.column = Clamp(1, next_mark.column, line.size+replace.size+1);
  }
  
  //- rjf: build+fill
  UI_NavTxtOp op = {0};
  {
    op.flags   = flags;
    op.replace = replace;
    op.copy    = copy;
    op.range   = range;
    op.cursor  = next_cursor;
    op.mark    = next_mark;
  }
  return op;
}

////////////////////////////////
//~ rjf: Single-Line String Modification

internal String8
ui_nav_push_string_replace_range(Arena *arena, String8 string, Rng1S64 col_range, String8 replace)
{
  //- rjf: convert to offset range
  Rng1U64 range =
  {
    (U64)(col_range.min-1),
    (U64)(col_range.max-1),
  };
  
  //- rjf: clamp range
  if(range.min > string.size)
  {
    range.min = 0;
  }
  if(range.max > string.size)
  {
    range.max = string.size;
  }
  
  //- rjf: calculate new size
  U64 old_size = string.size;
  U64 new_size = old_size - (range.max - range.min) + replace.size;
  
  //- rjf: push+fill new string storage
  U8 *push_base = push_array(arena, U8, new_size);
  {
    MemoryCopy(push_base+0, string.str, range.min);
    MemoryCopy(push_base+range.min+replace.size, string.str+range.max, string.size-range.max);
    if(replace.str != 0)
    {
      MemoryCopy(push_base+range.min, replace.str, replace.size);
    }
  }
  
  return str8(push_base, new_size);
}

////////////////////////////////
//~ rjf: Sizes

internal UI_Size
ui_size(UI_SizeKind kind, F32 value, F32 strictness)
{
  UI_Size size = {kind, value, strictness};
  return size;
}

////////////////////////////////
//~ rjf: Scroll Point Type Functions

internal UI_ScrollPt
ui_scroll_pt(S64 idx, F32 off)
{
  UI_ScrollPt pt = {idx, off};
  return pt;
}

internal void
ui_scroll_pt_target_idx(UI_ScrollPt *v, S64 idx)
{
  v->off = mod_f32(v->off, 1.f) + (F32)(v->idx+(S64)v->off - idx);
  v->idx = idx;
}

internal void
ui_scroll_pt_clamp_idx(UI_ScrollPt *v, Rng1S64 range)
{
  if(v->idx < range.min || range.max < v->idx)
  {
    S64 clamped = range.min;
    ui_scroll_pt_target_idx(v, clamped);
  }
}

////////////////////////////////
//~ rjf: Boxes

internal B32
ui_box_is_nil(UI_Box *box)
{
  return box == 0 || box == &ui_g_nil_box;
}

internal UI_BoxRec
ui_box_rec_df(UI_Box *box, UI_Box *root, U64 sib_member_off, U64 child_member_off)
{
  UI_BoxRec result = {0};
  result.next = &ui_g_nil_box;
  if(!ui_box_is_nil(*MemberFromOffset(UI_Box **, box, child_member_off)))
  {
    result.next = *MemberFromOffset(UI_Box **, box, child_member_off);
    result.push_count = 1;
  }
  else for(UI_Box *p = box; !ui_box_is_nil(p) && p != root; p = p->parent)
  {
    if(!ui_box_is_nil(*MemberFromOffset(UI_Box **, p, sib_member_off)))
    {
      result.next = *MemberFromOffset(UI_Box **, p, sib_member_off);
      break;
    }
    result.pop_count += 1;
  }
  return result;
}

internal void
ui_box_list_push(Arena *arena, UI_BoxList *list, UI_Box *box)
{
  UI_BoxNode *n = push_array(arena, UI_BoxNode, 1);
  n->box = box;
  SLLQueuePush(list->first, list->last, n);
  list->count += 1;
}

////////////////////////////////
//~ rjf: State Building / Selecting

internal UI_State *
ui_state_alloc(void)
{
  Arena *arena = arena_alloc();
  UI_State *ui = push_array(arena, UI_State, 1);
  ui->arena = arena;
  ui->build_arenas[0] = arena_alloc();
  ui->build_arenas[1] = arena_alloc();
  ui->drag_state_arena = arena_alloc();
  ui->string_hover_arena = arena_alloc();
  ui->box_table_size = 4096;
  ui->box_table = push_array(arena, UI_BoxHashSlot, ui->box_table_size);
  UI_InitStackNils(ui);
  return ui;
}

internal void
ui_state_release(UI_State *state)
{
  arena_release(state->string_hover_arena);
  arena_release(state->drag_state_arena);
  for(int i = 0; i < ArrayCount(state->build_arenas); i += 1)
  {
    arena_release(state->build_arenas[i]);
  }
  arena_release(state->arena);
}

internal UI_Box *
ui_root_from_state(UI_State *state)
{
  return state->root;
}

internal B32
ui_animating_from_state(UI_State *state)
{
  return state->is_animating;
}

internal void
ui_select_state(UI_State *state)
{
  ui_state = state;
}

internal UI_State *
ui_get_selected_state(void)
{
  return ui_state;
}

////////////////////////////////
//~ rjf: Implicit State Accessors/Mutators

//- rjf: per-frame info

internal Arena *
ui_build_arena(void)
{
  Arena *result = ui_state->build_arenas[ui_state->build_index%ArrayCount(ui_state->build_arenas)];
  return result;
}

internal OS_Handle
ui_window(void)
{
  return ui_state->window;
}

internal OS_EventList *
ui_events(void)
{
  return ui_state->events;
}

internal UI_NavActionList *
ui_nav_actions(void)
{
  return ui_state->nav_actions;
}

internal Vec2F32
ui_mouse(void)
{
  return ui_state->mouse;
}

internal F_Tag
ui_icon_font(void)
{
  return ui_state->icon_info.icon_font;
}

internal String8
ui_icon_string_from_kind(UI_IconKind icon_kind)
{
  return ui_state->icon_info.icon_kind_text_map[icon_kind];
}

internal F32
ui_dt(void)
{
  return ui_state->animation_dt;
}

//- rjf: drag data

internal Vec2F32
ui_drag_delta(void)
{
  return sub_2f32(ui_mouse(), ui_state->drag_start_mouse);
}

internal void
ui_store_drag_data(String8 string)
{
  arena_clear(ui_state->drag_state_arena);
  ui_state->drag_state_data = push_str8_copy(ui_state->drag_state_arena, string);
}

internal String8
ui_get_drag_data(U64 min_required_size)
{
  if(ui_state->drag_state_data.size < min_required_size)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 str = {push_array(scratch.arena, U8, min_required_size), min_required_size};
    ui_store_drag_data(str);
    scratch_end(scratch);
  }
  return ui_state->drag_state_data;
}

//- rjf: hovered string info

internal B32
ui_string_hover_active(void)
{
  return (ui_state->build_index > 0 && ui_state->string_hover_build_index >= ui_state->build_index-1 &&
          os_now_microseconds() >= ui_state->string_hover_begin_us + 500000);
}

internal U64
ui_string_hover_begin_time_us(void)
{
  return ui_state->string_hover_begin_us;
}

internal String8
ui_string_hover_string(Arena *arena)
{
  String8 result = push_str8_copy(arena, ui_state->string_hover_string);
  return result;
}

internal D_FancyRunList
ui_string_hover_runs(Arena *arena)
{
  D_FancyRunList result = d_fancy_run_list_copy(arena, &ui_state->string_hover_fancy_runs);
  return result;
}

//- rjf: interaction keys

internal UI_Key
ui_hot_key(void)
{
  return ui_state->hot_box_key;
}

internal UI_Key
ui_active_key(UI_MouseButtonKind button_kind)
{
  return ui_state->active_box_key[button_kind];
}

//- rjf: controls over interaction

internal void
ui_kill_action(void)
{
  for(EachEnumVal(UI_MouseButtonKind, k))
  {
    ui_state->active_box_key[k] = ui_key_zero();
  }
}

//- rjf: box cache lookup

internal UI_Box *
ui_box_from_key(UI_Key key)
{
  ProfBeginFunction();
  UI_Box *result = &ui_g_nil_box;
  if(!ui_key_match(key, ui_key_zero()))
  {
    U64 slot = key.u64[0] % ui_state->box_table_size;
    for(UI_Box *b = ui_state->box_table[slot].hash_first; !ui_box_is_nil(b); b = b->hash_next)
    {
      if(ui_key_match(b->key, key))
      {
        result = b;
        break;
      }
    }
  }
  ProfEnd();
  return result;
}

////////////////////////////////
//~ rjf: Top-Level Building API

internal void
ui_begin_build(OS_EventList *events, OS_Handle window, UI_NavActionList *nav_actions, UI_IconInfo *icon_info, F32 real_dt, F32 animation_dt)
{
  //- rjf: reset per-build ui state
  {
    UI_InitStacks(ui_state);
    ui_state->root = &ui_g_nil_box;
    ui_state->ctx_menu_touched_this_frame = 0;
    ui_state->is_animating = 0;
    ui_state->clipboard_copy_key = ui_key_zero();
    ui_state->last_build_box_count = ui_state->build_box_count;
    ui_state->build_box_count = 0;
    ui_state->tooltip_open = 0;
    ui_state->ctx_menu_changed = 0;
  }
  
  //- rjf: detect mouse-moves
  for(OS_Event *e = events->first; e != 0; e = e->next)
  {
    if(e->kind == OS_EventKind_MouseMove && os_handle_match(e->window, window))
    {
      ui_state->last_time_mousemoved_us = os_now_microseconds();
    }
  }
  
  //- rjf: fill build phase parameters
  {
    ui_state->events = events;
    ui_state->window = window;
    ui_state->nav_actions = nav_actions;
    ui_state->mouse = (os_window_is_focused(window) || ui_state->last_time_mousemoved_us+500000 >= os_now_microseconds()) ? os_mouse_from_window(window) : v2f32(-100, -100);
    ui_state->animation_dt = animation_dt;
    MemoryZeroStruct(&ui_state->icon_info);
    ui_state->icon_info.icon_font = icon_info->icon_font;
    for(UI_IconKind icon_kind = UI_IconKind_Null;
        icon_kind < UI_IconKind_COUNT;
        icon_kind = (UI_IconKind)(icon_kind + 1))
    {
      ui_state->icon_info.icon_kind_text_map[icon_kind] = push_str8_copy(ui_build_arena(), icon_info->icon_kind_text_map[icon_kind]);
    }
  }
  
  //- rjf: do default navigation
  {
    Temp scratch = scratch_begin(0, 0);
    if(!ui_key_match(ui_state->default_nav_root_key, ui_key_zero()))
    {
      UI_Box *nav_root = ui_box_from_key(ui_state->default_nav_root_key);
      if(!ui_box_is_nil(nav_root))
      {
        //- rjf: no child has the active focus -> do navigation at this layer
        if(ui_key_match(ui_key_zero(), nav_root->default_nav_focus_active_key))
        {
          for(;;)
          {
            B32 moved = 0;
            UI_Box *focus_box = ui_box_from_key(nav_root->default_nav_focus_next_hot_key);
            UI_BoxList next_focus_box_candidates = {0};
            
            // rjf: gather & consume events & nav actions
            B32 nav_next = 0;
            B32 nav_prev = 0;
            Axis2 axis_lock = Axis2_Invalid;
            if(os_key_press(events, window, 0, OS_Key_Tab))
            {
              nav_next = 1;
            }
            if(os_key_press(events, window, OS_EventFlag_Shift, OS_Key_Tab))
            {
              nav_prev = 1;
            }
            for(UI_NavActionNode *node = nav_actions->first, *next = 0;
                node != 0;
                node = next)
            {
              next = node->next;
              B32 taken = 0;
              if(node->v.delta.x == 0 && node->v.delta.y == 0)
              {
                continue;
              }
              if(((node->v.delta.x > 0 && nav_root->flags & UI_BoxFlag_DefaultFocusNavX) || node->v.delta.x == 0) &&
                 ((node->v.delta.y > 0 && nav_root->flags & UI_BoxFlag_DefaultFocusNavY) || node->v.delta.y == 0))
              {
                taken = 1;
                nav_next = 1;
              }
              if(((node->v.delta.x < 0 && nav_root->flags & UI_BoxFlag_DefaultFocusNavX) || node->v.delta.x == 0) &&
                 ((node->v.delta.y < 0 && nav_root->flags & UI_BoxFlag_DefaultFocusNavY) || node->v.delta.y == 0))
              {
                taken = 1;
                nav_prev = 1;
              }
              if(node->v.flags & UI_NavActionFlag_ExplicitDirectional)
              {
                axis_lock = node->v.delta.x != 0 ? Axis2_X : Axis2_Y;
              }
              if(taken)
              {
                ui_nav_eat_action_node(nav_actions, node);
              }
            }
            
            // rjf: [+] directional movement
            if(nav_next)
            {
              UI_Box *search_start = ui_box_is_nil(focus_box) ? nav_root : focus_box;
              U64 moved_in_axis[Axis2_COUNT] = {0};
              moved = 1;
              for(UI_Box *box = search_start;;)
              {
                if(box != search_start && !(box->flags & UI_BoxFlag_FocusNavSkip) && (box->flags & UI_BoxFlag_Clickable || ui_box_is_nil(box)) && (axis_lock == Axis2_Invalid || moved_in_axis[axis_lock] > 0))
                {
                  ui_box_list_push(scratch.arena, &next_focus_box_candidates, box);
                  if(axis_lock == Axis2_Invalid || moved_in_axis[axis_lock] > 1)
                  {
                    break;
                  }
                }
                UI_Box *last_box = box;
                if(!ui_box_is_nil(box->first))
                {
                  moved_in_axis[box->child_layout_axis] += 1;
                  box = box->first;
                }
                else for(UI_Box *p = box; !ui_box_is_nil(p) && p != nav_root; p = p->parent)
                {
                  if(!ui_box_is_nil(p->next))
                  {
                    moved_in_axis[p->parent->child_layout_axis] += 1;
                    box = p->next;
                    break;
                  }
                }
                if(last_box == box)
                {
                  ui_box_list_push(scratch.arena, &next_focus_box_candidates, &ui_g_nil_box);
                  break;
                }
              }
            }
            
            // rjf: [-] directional movement
            if(nav_prev)
            {
              UI_Box *search_start = ui_box_is_nil(focus_box) ? nav_root : focus_box;
              U64 moved_in_axis[Axis2_COUNT] = {0};
              moved = 1;
              for(UI_Box *box = search_start;;)
              {
                if(box != search_start && !(box->flags & UI_BoxFlag_FocusNavSkip) && (box->flags & UI_BoxFlag_Clickable || ui_box_is_nil(box)) && (axis_lock == Axis2_Invalid || moved_in_axis[axis_lock] > 0))
                {
                  ui_box_list_push(scratch.arena, &next_focus_box_candidates, box);
                  if(axis_lock == Axis2_Invalid || moved_in_axis[axis_lock] > 1)
                  {
                    break;
                  }
                }
                UI_Box *last_box = box;
                UI_Box *root_descendant = &ui_g_nil_box;
                if(box == nav_root && box == search_start)
                {
                  for(UI_Box *d = box->last; !ui_box_is_nil(d); d = d->last)
                  {
                    moved_in_axis[d->parent->child_layout_axis] += 1;
                    root_descendant = d;
                  }
                }
                UI_Box *prev_descendant = &ui_g_nil_box;
                for(UI_Box *d = box->prev; !ui_box_is_nil(d); d = d->last)
                {
                  moved_in_axis[d->parent->child_layout_axis] += 1;
                  prev_descendant = d;
                }
                if(!ui_box_is_nil(root_descendant))
                {
                  box = root_descendant;
                }
                else if(!ui_box_is_nil(prev_descendant))
                {
                  box = prev_descendant;
                }
                else if(box->parent != nav_root)
                {
                  moved_in_axis[box->parent->child_layout_axis] += 1;
                  box = box->parent;
                }
                if(box == last_box)
                {
                  ui_box_list_push(scratch.arena, &next_focus_box_candidates, &ui_g_nil_box);
                  break;
                }
              }
            }
            
            // rjf: scan candidates and grab next focus box
            UI_Box *next_focus_box = focus_box;
            F32 best_distance_from_start = 1000000;
            for(UI_BoxNode *n = next_focus_box_candidates.first; n != 0; n = n->next)
            {
              UI_Box *box = n->box;
              F32 distance_from_start = 0;
              if(axis_lock != Axis2_Invalid)
              {
                distance_from_start = abs_f32(center_2f32(box->rect).v[axis2_flip(axis_lock)] - center_2f32(focus_box->rect).v[axis2_flip(axis_lock)]);
              }
              if(distance_from_start < best_distance_from_start && box != focus_box)
              {
                next_focus_box = box;
                best_distance_from_start = distance_from_start;
              }
            }
            
            // rjf: commit next focus box
            nav_root->default_nav_focus_next_hot_key = next_focus_box->key;
            
            // rjf: no movement -> break
            if(moved == 0)
            {
              break;
            }
          }
        }
        
        //- rjf: some child has the active focus -> accept escape keys to pop from the active key stack
        if(!ui_key_match(ui_key_zero(), nav_root->default_nav_focus_active_key))
        {
          for(;os_key_press(events, window, 0, OS_Key_Esc);)
          {
            UI_Box *prev_focus_root = nav_root;
            for(UI_Box *focus_root = ui_box_from_key(nav_root->default_nav_focus_active_key);
                !ui_box_is_nil(focus_root);)
            {
              UI_Box *next_focus_root = ui_box_from_key(focus_root->default_nav_focus_active_key);
              if(ui_box_is_nil(next_focus_root))
              {
                prev_focus_root->default_nav_focus_next_active_key = ui_key_zero();
                break;
              }
              else
              {
                prev_focus_root = focus_root;
                focus_root = next_focus_root;
              }
            }
          }
        }
        
        //- rjf: some child has the active focus -> detect events which will cause an external focus commit
        // (e.g. clicking outside of a line edit)
        if(!ui_key_match(ui_key_zero(), nav_root->default_nav_focus_active_key))
        {
          UI_Box *active_box = ui_box_from_key(nav_root->default_nav_focus_active_key);
          if(!ui_box_is_nil(active_box))
          {
            for(OS_Event *event = events->first; event != 0; event = event->next)
            {
              if(!os_handle_match(event->window, window))
              {
                continue;
              }
              if(event->kind == OS_EventKind_Press &&
                 event->key == OS_Key_LeftMouseButton &&
                 !contains_2f32(active_box->rect, ui_mouse()))
              {
                ui_state->external_focus_commit = 1;
              }
            }
          }
        }
      }
    }
    ui_state->default_nav_root_key = ui_key_zero();
    scratch_end(scratch);
  }
  
  //- rjf: next-default-nav-focus keys -> current-default-nav-focus-keys
  for(U64 slot_idx = 0; slot_idx < ui_state->box_table_size; slot_idx += 1)
  {
    for(UI_Box *box = ui_state->box_table[slot_idx].hash_first;
        !ui_box_is_nil(box);
        box = box->hash_next)
    {
      box->default_nav_focus_hot_key = box->default_nav_focus_next_hot_key;
      box->default_nav_focus_active_key = box->default_nav_focus_next_active_key;
    }
  }
  
  //- rjf: build top-level root
  {
    Rng2F32 window_rect = os_client_rect_from_window(window);
    Vec2F32 window_rect_size = dim_2f32(window_rect);
    ui_set_next_fixed_width(window_rect_size.x);
    ui_set_next_fixed_height(window_rect_size.y);
    ui_set_next_child_layout_axis(Axis2_X);
    UI_Box *root = ui_build_box_from_stringf(0, "###%I64x", window.u64[0]);
    ui_push_parent(root);
    ui_state->root = root;
  }
  
  //- rjf: setup parent box for tooltip
  UI_FixedX(ui_state->mouse.x+15.f) UI_FixedY(ui_state->mouse.y) UI_PrefWidth(ui_children_sum(1.f)) UI_PrefHeight(ui_children_sum(1.f))
  {
    ui_set_next_child_layout_axis(Axis2_Y);
    ui_state->tooltip_root = ui_build_box_from_stringf(0, "###tooltip_%I64x", window.u64[0]);
  }
  
  //- rjf: setup parent box for context menu
  ui_state->ctx_menu_open = ui_state->next_ctx_menu_open;
  ui_state->ctx_menu_anchor_key = ui_state->next_ctx_menu_anchor_key;
  {
    UI_Box *anchor_box = ui_box_from_key(ui_state->ctx_menu_anchor_key);
    if(!ui_box_is_nil(anchor_box))
    {
      ui_state->ctx_menu_anchor_box_last_pos = anchor_box->rect.p0;
    }
    Vec2F32 anchor = add_2f32(ui_state->ctx_menu_anchor_box_last_pos, ui_state->ctx_menu_anchor_off);
    UI_FixedX(anchor.x) UI_FixedY(anchor.y) UI_PrefWidth(ui_children_sum(1.f)) UI_PrefHeight(ui_children_sum(1.f))
      UI_Focus(UI_FocusKind_On)
      UI_Squish(0.25f-ui_state->ctx_menu_open_t*0.25f)
      UI_Transparency(1-ui_state->ctx_menu_open_t)
    {
      ui_set_next_child_layout_axis(Axis2_Y);
      ui_state->ctx_menu_root = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawDropShadow|(ui_state->ctx_menu_open*UI_BoxFlag_DefaultFocusNavY), "###ctx_menu_%I64x", window.u64[0]);
    }
  }
  
  //- rjf: reset hot if we don't have an active widget
  {
    B32 has_active = 0;
    for(EachEnumVal(UI_MouseButtonKind, k))
    {
      if(!ui_key_match(ui_state->active_box_key[k], ui_key_zero()))
      {
        has_active = 1;
      }
    }
    if(!has_active)
    {
      ui_state->hot_box_key = ui_key_zero();
    }
  }
  
  //- rjf: reset active if our active box is disabled
  for(EachEnumVal(UI_MouseButtonKind, k))
  {
    if(!ui_key_match(ui_state->active_box_key[k], ui_key_zero()))
    {
      UI_Box *box = ui_box_from_key(ui_state->active_box_key[k]);
      if(!ui_box_is_nil(box) && box->flags & UI_BoxFlag_Disabled)
      {
        ui_state->active_box_key[k] = ui_key_zero();
      }
    }
  }
  
  //- rjf: reset active keys if they have been pruned
  for(EachEnumVal(UI_MouseButtonKind, k))
  {
    UI_Box *box = ui_box_from_key(ui_state->active_box_key[k]);
    if(ui_box_is_nil(box))
    {
      ui_state->active_box_key[k] = ui_key_zero();
    }
  }
  
  //- rjf: reset active keys if there is clicking activity on other windows
  for(OS_Event *event = events->first; event != 0; event = event->next)
  {
    if((event->kind == OS_EventKind_Press || event->kind == OS_EventKind_Release) &&
       !os_handle_match(event->window, window))
    {
      for(EachEnumVal(UI_MouseButtonKind, k))
      {
        ui_state->active_box_key[k] = ui_key_zero();
      }
      break;
    }
  }
}

internal void
ui_end_build(void)
{
  ProfBeginFunction();
  
  //- rjf: escape -> close context menu
  if(ui_state->ctx_menu_open != 0 && os_key_press(ui_events(), ui_window(), 0, OS_Key_Esc))
  {
    ui_ctx_menu_close();
  }
  
  //- rjf: prune untouched or transient widgets in the cache
  {
    ProfBegin("ui prune unused widgets");
    for(U64 slot_idx = 0; slot_idx < ui_state->box_table_size; slot_idx += 1)
    {
      for(UI_Box *box = ui_state->box_table[slot_idx].hash_first, *next = 0;
          !ui_box_is_nil(box);
          box = next)
      {
        next = box->hash_next;
        if(box->last_touched_build_index < ui_state->build_index ||
           ui_key_match(box->key, ui_key_zero()))
        {
          DLLRemove_NPZ(&ui_g_nil_box, ui_state->box_table[slot_idx].hash_first, ui_state->box_table[slot_idx].hash_last, box, hash_next, hash_prev);
          SLLStackPush(ui_state->first_free_box, box);
        }
      }
    }
    ProfEnd();
  }
  
  //- rjf: layout box tree
  {
    ProfBegin("ui box tree layout");
    for(Axis2 axis = (Axis2)0; axis < Axis2_COUNT; axis = (Axis2)(axis + 1))
    {
      ui_layout_root(ui_state->root, axis);
    }
    ProfEnd();
  }
  
  //- rjf: close ctx menu if untouched
  if(!ui_state->ctx_menu_touched_this_frame)
  {
    ui_ctx_menu_close();
  }
  
  //- rjf: stick ctx menu to anchor
  if(ui_state->ctx_menu_touched_this_frame && !ui_state->ctx_menu_changed)
  {
    UI_Box *anchor_box = ui_box_from_key(ui_state->ctx_menu_anchor_key);
    if(!ui_box_is_nil(anchor_box))
    {
      Rng2F32 root_rect = ui_state->ctx_menu_root->rect;
      Vec2F32 pos =
      {
        anchor_box->rect.x0 + ui_state->ctx_menu_anchor_off.x,
        anchor_box->rect.y0 + ui_state->ctx_menu_anchor_off.y,
      };
      Vec2F32 shift = sub_2f32(pos, root_rect.p0);
      Rng2F32 new_root_rect = shift_2f32(root_rect, shift);
      ui_state->ctx_menu_root->fixed_position = new_root_rect.p0;
      ui_state->ctx_menu_root->fixed_size = dim_2f32(new_root_rect);
      ui_state->ctx_menu_root->rect = new_root_rect;
    }
  }
  
  //- rjf: ensure special floating roots are within screen bounds
  UI_Box *floating_roots[] = {ui_state->tooltip_root, ui_state->ctx_menu_root};
  for(U64 idx = 0; idx < ArrayCount(floating_roots); idx += 1)
  {
    UI_Box *root = floating_roots[idx];
    if(!ui_box_is_nil(root))
    {
      Rng2F32 window_rect = os_client_rect_from_window(ui_window());
      Rng2F32 root_rect = root->rect;
      Vec2F32 shift =
      {
        -ClampBot(0, root_rect.x1 - window_rect.x1),
        -ClampBot(0, root_rect.y1 - window_rect.y1),
      };
      Rng2F32 new_root_rect = shift_2f32(root_rect, shift);
      root->fixed_position = new_root_rect.p0;
      root->fixed_size = dim_2f32(new_root_rect);
      root->rect = new_root_rect;
      for(Axis2 axis = (Axis2)0; axis < Axis2_COUNT; axis = (Axis2)(axis + 1))
      {
        ui_calc_sizes_standalone__in_place_rec(root, axis);
        ui_calc_sizes_upwards_dependent__in_place_rec(root, axis);
        ui_calc_sizes_downwards_dependent__in_place_rec(root, axis);
        ui_layout_enforce_constraints__in_place_rec(root, axis);
        ui_layout_position__in_place_rec(root, axis);
      }
    }
  }
  
  //- rjf: enforce child-rounding
  {
    for(U64 slot_idx = 0; slot_idx < ui_state->box_table_size; slot_idx += 1)
    {
      for(UI_Box *box = ui_state->box_table[slot_idx].hash_first;
          !ui_box_is_nil(box);
          box = box->hash_next)
      {
        if(box->flags & UI_BoxFlag_RoundChildrenByParent &&
           !ui_box_is_nil(box->first) && !ui_box_is_nil(box->last))
        {
          box->first->corner_radii[Corner_00] = box->corner_radii[Corner_00];
          box->first->corner_radii[Corner_10] = box->corner_radii[Corner_10];
          box->last->corner_radii[Corner_01] = box->corner_radii[Corner_01];
          box->last->corner_radii[Corner_11] = box->corner_radii[Corner_11];
        }
      }
    }
  }
  
  //- rjf: animate
  {
    ProfBegin("ui animate");
    F32 vast_rate = 1 - pow_f32(2, (-60.f * ui_state->animation_dt));
    F32 fast_rate = 1 - pow_f32(2, (-50.f * ui_state->animation_dt));
    F32 fish_rate = 1 - pow_f32(2, (-40.f * ui_state->animation_dt));
    F32 slow_rate = 1 - pow_f32(2, (-30.f * ui_state->animation_dt));
    F32 slug_rate = 1 - pow_f32(2, (-15.f * ui_state->animation_dt));
    F32 slaf_rate = 1 - pow_f32(2, (-8.f * ui_state->animation_dt));
    ui_state->ctx_menu_open_t += ((F32)!!ui_state->ctx_menu_open - ui_state->ctx_menu_open_t) * vast_rate;
    ui_state->is_animating = (ui_state->is_animating || abs_f32((F32)!!ui_state->ctx_menu_open - ui_state->ctx_menu_open_t) > 0.01f);
    if(ui_state->ctx_menu_open_t >= 0.99f && ui_state->ctx_menu_open)
    {
      ui_state->ctx_menu_open_t = 1.f;
    }
    ui_state->tooltip_open_t += ((F32)!!ui_state->tooltip_open - ui_state->tooltip_open_t) * vast_rate;
    ui_state->is_animating = (ui_state->is_animating || abs_f32((F32)!!ui_state->tooltip_open - ui_state->tooltip_open_t) > 0.01f);
    if(ui_state->tooltip_open_t >= 0.99f && ui_state->tooltip_open)
    {
      ui_state->tooltip_open_t = 1.f;
    }
    for(U64 slot_idx = 0; slot_idx < ui_state->box_table_size; slot_idx += 1)
    {
      for(UI_Box *box = ui_state->box_table[slot_idx].hash_first;
          !ui_box_is_nil(box);
          box = box->hash_next)
      {
        // rjf: grab states informing animation
        B32 is_hot            = ui_key_match(box->key, ui_state->hot_box_key);
        B32 is_active         = ui_key_match(box->key, ui_state->active_box_key[UI_MouseButtonKind_Left]);
        B32 is_disabled       = !!(box->flags & UI_BoxFlag_Disabled) && (box->first_disabled_build_index+10 < ui_state->build_index ||
                                                                         box->first_touched_build_index == box->first_disabled_build_index);
        B32 is_focus_hot      = !!(box->flags & UI_BoxFlag_FocusHot) && !(box->flags & UI_BoxFlag_FocusHotDisabled);
        B32 is_focus_active   = !!(box->flags & UI_BoxFlag_FocusActive) && !(box->flags & UI_BoxFlag_FocusActiveDisabled);
        B32 is_focus_active_disabled = !!(box->flags & UI_BoxFlag_FocusActiveDisabled);
        
        // rjf: determine rates
        F32 hot_rate      = fast_rate;
        F32 active_rate   = fast_rate;
        F32 disabled_rate = slow_rate;
        F32 focus_rate    = (is_focus_hot || is_focus_active) ? fast_rate : fast_rate;
        
        // rjf: determine animating status
        B32 box_is_animating = 0;
        box_is_animating = (box_is_animating || abs_f32((F32)is_hot          - box->hot_t) > 0.01f);
        box_is_animating = (box_is_animating || abs_f32((F32)is_active       - box->active_t) > 0.01f);
        box_is_animating = (box_is_animating || abs_f32((F32)is_disabled     - box->disabled_t) > 0.01f);
        box_is_animating = (box_is_animating || abs_f32((F32)is_focus_hot    - box->focus_hot_t) > 0.01f);
        box_is_animating = (box_is_animating || abs_f32((F32)is_focus_active - box->focus_active_t) > 0.01f);
        box_is_animating = (box_is_animating || abs_f32((F32)is_focus_active_disabled - box->focus_active_disabled_t) > 0.01f);
        box_is_animating = (box_is_animating || abs_f32(box->view_off_target.x - box->view_off.x) > 0.5f);
        box_is_animating = (box_is_animating || abs_f32(box->view_off_target.y - box->view_off.y) > 0.5f);
        if(box->flags & UI_BoxFlag_AnimatePosX)
        {
          box_is_animating = (box_is_animating || abs_f32(box->fixed_position_animated.x - box->fixed_position.x) > 0.5f);
        }
        if(box->flags & UI_BoxFlag_AnimatePosY)
        {
          box_is_animating = (box_is_animating || abs_f32(box->fixed_position_animated.y - box->fixed_position.y) > 0.5f);
        }
        ui_state->is_animating = (ui_state->is_animating || box_is_animating);
#if 0 // NOTE(rjf): enable to debug animation-causing-frames (or not)
        if(box_is_animating)
        {
          box->overlay_color = v4f32(1, 0, 0, 0.1f);
          box->flags |= UI_BoxFlag_DrawOverlay;
        }
#endif
        
        // rjf: animate interaction transition states
        box->hot_t                   += hot_rate      * ((F32)is_hot - box->hot_t);
        box->active_t                += active_rate   * ((F32)is_active - box->active_t);
        box->disabled_t              += disabled_rate * ((F32)is_disabled - box->disabled_t);
        box->focus_hot_t             += focus_rate    * ((F32)is_focus_hot - box->focus_hot_t);
        box->focus_active_t          += focus_rate    * ((F32)is_focus_active - box->focus_active_t);
        box->focus_active_disabled_t += focus_rate    * ((F32)is_focus_active_disabled - box->focus_active_disabled_t);
        
        // rjf: animate positions
        {
          box->fixed_position_animated.x += fast_rate * (box->fixed_position.x - box->fixed_position_animated.x);
          box->fixed_position_animated.y += fast_rate * (box->fixed_position.y - box->fixed_position_animated.y);
          if(abs_f32(box->fixed_position.x - box->fixed_position_animated.x) < 1)
          {
            box->fixed_position_animated.x = box->fixed_position.x;
          }
          if(abs_f32(box->fixed_position.y - box->fixed_position_animated.y) < 1)
          {
            box->fixed_position_animated.y = box->fixed_position.y;
          }
        }
        
        // rjf: clamp view
        if(box->flags & UI_BoxFlag_ViewClamp)
        {
          Vec2F32 max_view_off_target =
          {
            ClampBot(0, box->view_bounds.x - box->fixed_size.x),
            ClampBot(0, box->view_bounds.y - box->fixed_size.y),
          };
          if(box->flags & UI_BoxFlag_ViewClampX) { box->view_off_target.x = Clamp(0, box->view_off_target.x, max_view_off_target.x); }
          if(box->flags & UI_BoxFlag_ViewClampY) { box->view_off_target.y = Clamp(0, box->view_off_target.y, max_view_off_target.y); }
        }
        
        // rjf: animate view offset
        {
          box->view_off.x += fast_rate * (box->view_off_target.x - box->view_off.x);
          box->view_off.y += fast_rate * (box->view_off_target.y - box->view_off.y);
          if(abs_f32(box->view_off.x - box->view_off_target.x) < 2)
          {
            box->view_off.x = box->view_off_target.x;
          }
          if(abs_f32(box->view_off.y - box->view_off_target.y) < 2)
          {
            box->view_off.y = box->view_off_target.y;
          }
        }
      }
    }
    ProfEnd();
  }
  
  //- rjf: animate context menu
  if(ui_state->ctx_menu_open && !ui_box_is_nil(ui_state->ctx_menu_root) && !ui_state->ctx_menu_changed)
  {
    UI_Box *root = ui_state->ctx_menu_root;
    Rng2F32 rect = root->rect;
    root->rect.y1 = root->rect.y0 + dim_2f32(rect).y * ui_state->ctx_menu_open_t;
  }
  
  //- rjf: fall-through interact with context menu
  if(ui_state->ctx_menu_open)
  {
    ui_signal_from_box(ui_state->ctx_menu_root);
  }
  
  //- rjf: close ctx menu if unconsumed clicks
  for(OS_Event *event = ui_events()->first; event != 0; event = event->next)
  {
    if(event->kind == OS_EventKind_Press && os_handle_match(event->window, ui_window()) &&
       (event->key == OS_Key_LeftMouseButton || event->key == OS_Key_RightMouseButton))
    {
      ui_ctx_menu_close();
    }
  }
  
  //- rjf: hover cursor
  {
    UI_Box *hot = ui_box_from_key(ui_state->hot_box_key);
    UI_Box *active = ui_box_from_key(ui_state->active_box_key[UI_MouseButtonKind_Left]);
    UI_Box *box = ui_box_is_nil(active) ? hot : active;
    OS_Cursor cursor = box->hover_cursor;
    if(box->flags & UI_BoxFlag_Disabled && box->flags & UI_BoxFlag_Clickable)
    {
      cursor = OS_Cursor_Disabled;
    }
    if(os_window_is_focused(ui_state->window) || !ui_box_is_nil(active))
    {
      os_set_cursor(cursor);
    }
  }
  
  //- rjf: clipboard commits
  {
    Temp scratch = scratch_begin(0, 0);
    UI_Box *box = ui_box_from_key(ui_state->clipboard_copy_key);
    String8List strs = {0};
    {
      UI_BoxRec rec = {0};
      for(UI_Box *b = box; !ui_box_is_nil(b); rec = ui_box_rec_df_pre(b, box), b = rec.next)
      {
        if(b->flags & UI_BoxFlag_DrawText && b->flags & UI_BoxFlag_HasDisplayString && !f_tag_match(b->font, ui_icon_font()))
        {
          String8 display_string = ui_box_display_string(b);
          str8_list_push(scratch.arena, &strs, display_string);
        }
      }
      if(strs.node_count != 0)
      {
        StringJoin join = {0};
        join.sep = str8_lit(" ");
        String8 string = str8_list_join(scratch.arena, &strs, &join);
        os_set_clipboard_text(string);
      }
    }
    scratch_end(scratch);
  }
  
  //- rjf: hovering possibly-truncated drawn text -> store text
  {
    B32 inactive = 1;
    for(EachEnumVal(UI_MouseButtonKind, k))
    {
      if(!ui_key_match(ui_key_zero(), ui_state->active_box_key[k]))
      {
        inactive = 0;
        break;
      }
    }
    if(inactive)
    {
      B32 found = 0;
      for(UI_Box *box = ui_state->root, *next = 0; !ui_box_is_nil(box); box = next)
      {
        UI_BoxRec rec = ui_box_rec_df_pre(box, ui_state->root);
        next = rec.next;
        S32 pop_idx = 0;
        for(UI_Box *b = box; !ui_box_is_nil(b) && pop_idx <= rec.pop_count; b = b->parent, pop_idx += 1)
        {
          if(b->flags & UI_BoxFlag_DrawText && !(b->flags & UI_BoxFlag_DisableTextTrunc))
          {
            String8 box_display_string = ui_box_display_string(b);
            Vec2F32 text_pos = ui_box_text_position(b);
            Vec2F32 drawn_text_dim = b->display_string_runs.dim;
            B32 text_is_truncated = (drawn_text_dim.x + text_pos.x > b->rect.x1);
            B32 mouse_is_hovering = contains_2f32(r2f32p(text_pos.x,
                                                         b->rect.y0,
                                                         Min(text_pos.x+drawn_text_dim.x, b->rect.x1),
                                                         b->rect.y1),
                                                  ui_state->mouse);
            if(text_is_truncated && mouse_is_hovering)
            {
              if(!str8_match(box_display_string, ui_state->string_hover_string, 0))
              {
                arena_clear(ui_state->string_hover_arena);
                ui_state->string_hover_string = push_str8_copy(ui_state->string_hover_arena, box_display_string);
                ui_state->string_hover_fancy_runs = d_fancy_run_list_copy(ui_state->string_hover_arena, &b->display_string_runs);
                ui_state->string_hover_begin_us = os_now_microseconds();
              }
              ui_state->string_hover_build_index = ui_state->build_index;
              found = 1;
              goto break_all_hover_string;
            }
          }
          if(b != box && contains_2f32(b->rect, ui_state->mouse) && b->flags & UI_BoxFlag_DrawText)
          {
            goto break_all_hover_string;
          }
        }
      }
      break_all_hover_string:;
      if(!found)
      {
        arena_clear(ui_state->string_hover_arena);
        ui_state->string_hover_build_index = 0;
        MemoryZeroStruct(&ui_state->string_hover_string);
      }
      if(found && !ui_string_hover_active())
      {
        ui_state->is_animating = 1;
      }
    }
  }
  
  ui_state->build_index += 1;
  arena_clear(ui_build_arena());
  ProfEnd();
}

internal void
ui_calc_sizes_standalone__in_place_rec(UI_Box *root, Axis2 axis)
{
  ProfBeginFunction();
  
  switch(root->pref_size[axis].kind)
  {
    default:{}break;
    case UI_SizeKind_Pixels:
    {
      root->fixed_size.v[axis] = root->pref_size[axis].value;
    }break;
    
    case UI_SizeKind_TextContent:
    {
      F32 padding = root->pref_size[axis].value;
      F32 text_size = root->display_string_runs.dim.x;
      root->fixed_size.v[axis] = padding + text_size;
    }break;
  }
  
  //- rjf: recurse
  for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
  {
    ui_calc_sizes_standalone__in_place_rec(child, axis);
  }
  
  ProfEnd();
}

internal void
ui_calc_sizes_upwards_dependent__in_place_rec(UI_Box *root, Axis2 axis)
{
  ProfBeginFunction();
  
  //- rjf: solve for all kinds that are upwards-dependent
  switch(root->pref_size[axis].kind)
  {
    default: break;
    
    // rjf: if root has a parent percentage, figure out its size
    case UI_SizeKind_ParentPct:
    {
      // rjf: find parent that has a fixed size
      UI_Box *fixed_parent = &ui_g_nil_box;
      for(UI_Box *p = root->parent; !ui_box_is_nil(p); p = p->parent)
      {
        if(p->flags & (UI_BoxFlag_FixedWidth<<axis) ||
           p->pref_size[axis].kind == UI_SizeKind_Pixels ||
           p->pref_size[axis].kind == UI_SizeKind_TextContent ||
           p->pref_size[axis].kind == UI_SizeKind_ParentPct)
        {
          fixed_parent = p;
          break;
        }
      }
      
      // rjf: figure out root's size on this axis
      F32 size = fixed_parent->fixed_size.v[axis] * root->pref_size[axis].value;
      
      // rjf: mutate root to have this size
      root->fixed_size.v[axis] = size;
    }break;
  }
  
  //- rjf: recurse
  for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
  {
    ui_calc_sizes_upwards_dependent__in_place_rec(child, axis);
  }
  
  ProfEnd();
}

internal void
ui_calc_sizes_downwards_dependent__in_place_rec(UI_Box *root, Axis2 axis)
{
  ProfBeginFunction();
  
  //- rjf: recurse first. we may depend on children that have
  // the same property
  for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
  {
    ui_calc_sizes_downwards_dependent__in_place_rec(child, axis);
  }
  
  //- rjf: solve for all kinds that are downwards-dependent
  switch(root->pref_size[axis].kind)
  {
    default: break;
    
    // rjf: sum children
    case UI_SizeKind_ChildrenSum:
    {
      F32 sum = 0;
      for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
      {
        if(!(child->flags & (UI_BoxFlag_FloatingX<<axis)))
        {
          if(axis == root->child_layout_axis)
          {
            sum += child->fixed_size.v[axis];
          }
          else
          {
            sum = Max(sum, child->fixed_size.v[axis]);
          }
        }
      }
      
      // rjf: figure out root's size on this axis
      root->fixed_size.v[axis] = sum;
    }break;
  }
  
  ProfEnd();
}

internal void
ui_layout_enforce_constraints__in_place_rec(UI_Box *root, Axis2 axis)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  
  // NOTE(rjf): The "layout axis" is the direction in which children
  // of some node are intended to be laid out.
  
  //- rjf: fixup children sizes (if we're solving along the *non-layout* axis)
  if(axis != root->child_layout_axis && !(root->flags & (UI_BoxFlag_AllowOverflowX << axis)))
  {
    F32 allowed_size = root->fixed_size.v[axis];
    for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
    {
      if(!(child->flags & (UI_BoxFlag_FloatingX<<axis)))
      {
        F32 child_size = child->fixed_size.v[axis];
        F32 violation = child_size - allowed_size;
        F32 max_fixup = child_size;
        F32 fixup = Clamp(0, violation, max_fixup);
        if(fixup > 0)
        {
          child->fixed_size.v[axis] -= fixup;
        }
      }
    }
    
  }
  
  //- rjf: fixup children sizes (in the direction of the layout axis)
  if(axis == root->child_layout_axis && !(root->flags & (UI_BoxFlag_AllowOverflowX << axis)))
  {
    // rjf: figure out total allowed size & total size
    F32 total_allowed_size = root->fixed_size.v[axis];
    F32 total_size = 0;
    F32 total_weighted_size = 0;
    for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
    {
      if(!(child->flags & (UI_BoxFlag_FloatingX<<axis)))
      {
        total_size += child->fixed_size.v[axis];
        total_weighted_size += child->fixed_size.v[axis] * (1-child->pref_size[axis].strictness);
      }
    }
    
    // rjf: if we have a violation, we need to subtract some amount from all children
    F32 violation = total_size - total_allowed_size;
    if(violation > 0)
    {
      // rjf: figure out how much we can take in totality
      F32 child_fixup_sum = 0;
      F32 *child_fixups = push_array(scratch.arena, F32, root->child_count);
      {
        U64 child_idx = 0;
        for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next, child_idx += 1)
        {
          if(!(child->flags & (UI_BoxFlag_FloatingX<<axis)))
          {
            F32 fixup_size_this_child = child->fixed_size.v[axis] * (1-child->pref_size[axis].strictness);
            fixup_size_this_child = ClampBot(0, fixup_size_this_child);
            child_fixups[child_idx] = fixup_size_this_child;
            child_fixup_sum += fixup_size_this_child;
          }
        }
      }
      
      // rjf: fixup child sizes
      {
        U64 child_idx = 0;
        for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next, child_idx += 1)
        {
          if(!(child->flags & (UI_BoxFlag_FloatingX<<axis)))
          {
            F32 fixup_pct = (violation / total_weighted_size);
            fixup_pct = Clamp(0, fixup_pct, 1);
            child->fixed_size.v[axis] -= child_fixups[child_idx] * fixup_pct;
            child->fixed_size.v[axis] = child->fixed_size.v[axis];
          }
        }
      }
    }
    
  }
  
  //- rjf: fixup upwards-relative sizes
  if(root->flags & (UI_BoxFlag_AllowOverflowX << axis))
  {
    for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
    {
      if(child->pref_size[axis].kind == UI_SizeKind_ParentPct)
      {
        child->fixed_size.v[axis] = root->fixed_size.v[axis] * child->pref_size[axis].value;
      }
    }
  }
  
  //- rjf: recurse
  for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
  {
    ui_layout_enforce_constraints__in_place_rec(child, axis);
  }
  
  scratch_end(scratch);
  ProfEnd();
}

internal void
ui_layout_position__in_place_rec(UI_Box *root, Axis2 axis)
{
  ProfBeginFunction();
  F32 layout_position = 0;
  
  //- rjf: lay out children
  F32 bounds = 0;
  for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
  {
    // rjf: grab original position
    F32 original_position = Min(child->rect.p0.v[axis], child->rect.p1.v[axis]);
    
    // rjf: calculate fixed position & size
    if(!(child->flags & (UI_BoxFlag_FloatingX<<axis)))
    {
      child->fixed_position.v[axis] = layout_position;
      if(root->child_layout_axis == axis)
      {
        layout_position += child->fixed_size.v[axis];
        bounds += child->fixed_size.v[axis];
      }
      else
      {
        bounds = Max(bounds, child->fixed_size.v[axis]);
      }
    }
    
    // rjf: determine final rect for child, given fixed_position & size
    if(child->flags & (UI_BoxFlag_AnimatePosX<<axis))
    {
      if(child->first_touched_build_index == child->last_touched_build_index)
      {
        child->fixed_position_animated = child->fixed_position;
      }
      child->rect.p0.v[axis] = root->rect.p0.v[axis] + child->fixed_position_animated.v[axis] - !(child->flags&(UI_BoxFlag_SkipViewOffX<<axis))*root->view_off.v[axis];
    }
    else
    {
      child->rect.p0.v[axis] = root->rect.p0.v[axis] + child->fixed_position.v[axis] - !(child->flags&(UI_BoxFlag_SkipViewOffX<<axis))*root->view_off.v[axis];
    }
    child->rect.p1.v[axis] = child->rect.p0.v[axis] + child->fixed_size.v[axis];
    child->rect.p0.x = floorf(child->rect.p0.x);
    child->rect.p0.y = floorf(child->rect.p0.y);
    child->rect.p1.x = floorf(child->rect.p1.x);
    child->rect.p1.y = floorf(child->rect.p1.y);
    
    // rjf: grab new position
    F32 new_position = Min(child->rect.p0.v[axis], child->rect.p1.v[axis]);
    
    // rjf: store position delta
    child->position_delta.v[axis] = new_position - original_position;
  }
  
  //- rjf: store view bounds
  {
    root->view_bounds.v[axis] = bounds;
  }
  
  //- rjf: recurse
  for(UI_Box *child = root->first; !ui_box_is_nil(child); child = child->next)
  {
    ui_layout_position__in_place_rec(child, axis);
  }
  
  ProfEnd();
}

internal void
ui_layout_root(UI_Box *root, Axis2 axis)
{
  ProfBegin("ui layout pass (%s)", axis == Axis2_X ? "x" : "y");
  ui_calc_sizes_standalone__in_place_rec(root, axis);
  ui_calc_sizes_upwards_dependent__in_place_rec(root, axis);
  ui_calc_sizes_downwards_dependent__in_place_rec(root, axis);
  ui_layout_enforce_constraints__in_place_rec(root, axis);
  ui_layout_position__in_place_rec(root, axis);
  ProfEnd();
}

////////////////////////////////
//~ rjf: Box Building API

//- rjf: tooltips

internal void
ui_tooltip_begin_base(void)
{
  ui_state->tooltip_open = 1;
  ui_push_parent(ui_root_from_state(ui_state));
  ui_push_parent(ui_state->tooltip_root);
  ui_push_flags(0);
}

internal void
ui_tooltip_end_base(void)
{
  ui_pop_flags();
  ui_pop_transparency();
  ui_pop_parent();
  ui_pop_parent();
}

internal void
ui_tooltip_begin(void)
{
  ui_tooltip_begin_base();
  ui_set_next_squish(0.25f-ui_state->tooltip_open_t*0.25f);
  ui_set_next_transparency(1-ui_state->tooltip_open_t);
  UI_Flags(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBackgroundBlur|UI_BoxFlag_DrawDropShadow)
    UI_PrefWidth(ui_children_sum(1))
    UI_PrefHeight(ui_children_sum(1))
    UI_CornerRadius(ui_top_font_size()*0.25f)
    ui_column_begin();
  UI_PrefWidth(ui_px(0, 1)) ui_spacer(ui_em(0.5f, 1.f));
  UI_PrefWidth(ui_children_sum(1))
    UI_PrefHeight(ui_children_sum(1))
    ui_row_begin();
  UI_PrefHeight(ui_px(0, 1)) ui_spacer(ui_em(0.5f, 1.f));
  UI_PrefWidth(ui_children_sum(1))
    UI_PrefHeight(ui_children_sum(1))
    ui_column_begin();
  ui_push_pref_width(ui_text_dim(10.f, 1.f));
  ui_push_pref_height(ui_em(2.f, 1.f));
  ui_push_text_alignment(UI_TextAlign_Center);
}

internal void
ui_tooltip_end(void)
{
  ui_pop_text_alignment();
  ui_pop_pref_width();
  ui_pop_pref_height();
  ui_column_end();
  UI_PrefHeight(ui_px(0, 1)) ui_spacer(ui_em(0.5f, 1.f));
  ui_row_end();
  UI_PrefWidth(ui_px(0, 1)) ui_spacer(ui_em(0.5f, 1.f));
  ui_column_end();
  ui_tooltip_end_base();
}

//- rjf: context menus

internal void
ui_ctx_menu_open(UI_Key key, UI_Key anchor_box_key, Vec2F32 anchor_off)
{
  anchor_off.x = (F32)(int)anchor_off.x;
  anchor_off.y = (F32)(int)anchor_off.y;
  ui_state->next_ctx_menu_open = 1;
  ui_state->ctx_menu_changed = 1;
  ui_state->ctx_menu_open_t = 0;
  ui_state->ctx_menu_key = key;
  ui_state->next_ctx_menu_anchor_key = anchor_box_key;
  ui_state->ctx_menu_anchor_off = anchor_off;
  ui_state->ctx_menu_touched_this_frame = 1;
  ui_state->ctx_menu_anchor_box_last_pos = v2f32(0, 0);
  ui_state->ctx_menu_root->default_nav_focus_active_key = ui_key_zero();
  ui_state->ctx_menu_root->default_nav_focus_next_active_key = ui_key_zero();
}

internal void
ui_ctx_menu_close(void)
{
  ui_state->next_ctx_menu_open = 0;
}

internal B32
ui_begin_ctx_menu(UI_Key key)
{
  ui_push_parent(ui_root_from_state(ui_state));
  ui_push_parent(ui_state->ctx_menu_root);
  B32 result = ui_key_match(key, ui_state->ctx_menu_key) && ui_state->ctx_menu_open;
  if(result != 0)
  {
    ui_state->ctx_menu_touched_this_frame = 1;
    ui_state->ctx_menu_root->flags |= UI_BoxFlag_RoundChildrenByParent;
    ui_state->ctx_menu_root->flags |= UI_BoxFlag_DrawBackgroundBlur;
    ui_state->ctx_menu_root->flags |= UI_BoxFlag_DrawBackground;
    ui_state->ctx_menu_root->flags |= UI_BoxFlag_DrawBorder;
    ui_state->ctx_menu_root->flags |= UI_BoxFlag_Clip;
    ui_state->ctx_menu_root->flags |= UI_BoxFlag_Clickable;
    ui_state->ctx_menu_root->corner_radii[Corner_00] = ui_state->ctx_menu_root->corner_radii[Corner_01] = ui_state->ctx_menu_root->corner_radii[Corner_10] = ui_state->ctx_menu_root->corner_radii[Corner_11] = ui_top_font_size()*0.25f;
    ui_state->ctx_menu_root->background_color = ui_top_background_color();
    ui_state->ctx_menu_root->border_color = ui_top_border_color();
    ui_state->ctx_menu_root->blur_size = ui_top_blur_size();
  }
  ui_push_pref_width(ui_bottom_pref_width());
  ui_push_pref_height(ui_bottom_pref_height());
  ui_push_focus_hot(UI_FocusKind_Root);
  ui_push_focus_active(UI_FocusKind_Root);
  return result;
}

internal void
ui_end_ctx_menu(void)
{
  ui_pop_focus_active();
  ui_pop_focus_hot();
  ui_pop_pref_width();
  ui_pop_pref_height();
  ui_pop_parent();
  ui_pop_parent();
}

internal B32
ui_ctx_menu_is_open(UI_Key key)
{
  return (ui_state->ctx_menu_open && ui_key_match(key, ui_state->ctx_menu_key));
}

internal B32
ui_any_ctx_menu_is_open(void)
{
  return ui_state->ctx_menu_open;
}

//- rjf: focus tree coloring

internal B32
ui_is_focus_hot(void)
{
  B32 result = (ui_state->focus_hot_stack.top->v == UI_FocusKind_On);
  if(result)
  {
    for(UI_FocusHotNode *n = ui_state->focus_hot_stack.top; n != 0; n = n->next)
    {
      if(n->v == UI_FocusKind_Root)
      {
        break;
      }
      if(n->v == UI_FocusKind_Off)
      {
        result = 0;
        break;
      }
    }
  }
  return result;
}

internal B32
ui_is_focus_active(void)
{
  B32 result = (ui_state->focus_active_stack.top->v == UI_FocusKind_On);
  if(result)
  {
    for(UI_FocusActiveNode *n = ui_state->focus_active_stack.top; n != 0; n = n->next)
    {
      if(n->v == UI_FocusKind_Root)
      {
        break;
      }
      if(n->v == UI_FocusKind_Off)
      {
        result = 0;
        break;
      }
    }
  }
  return result;
}

//- rjf: implicit auto-managed tree-based focus state

internal B32
ui_is_key_auto_focus_active(UI_Key key)
{
  B32 result = 0;
  if(!ui_key_match(ui_key_zero(), key))
  {
    for(UI_Box *p = ui_top_parent(); !ui_box_is_nil(p); p = p->parent)
    {
      if(p->flags & UI_BoxFlag_FocusActive && ui_key_match(key, p->default_nav_focus_active_key))
      {
        result = 1;
        break;
      }
    }
  }
  return result;
}

internal B32
ui_is_key_auto_focus_hot(UI_Key key)
{
  B32 result = 0;
  if(!ui_key_match(ui_key_zero(), key))
  {
    for(UI_Box *p = ui_top_parent(); !ui_box_is_nil(p); p = p->parent)
    {
      if(p->flags & UI_BoxFlag_FocusHot &&
         ((!(p->flags & UI_BoxFlag_FocusHotDisabled) &&
           ui_key_match(key, p->default_nav_focus_hot_key)) ||
          ui_key_match(key, p->default_nav_focus_active_key)))
      {
        result = 1;
        break;
      }
    }
  }
  return result;
}

internal void
ui_set_auto_focus_active_key(UI_Key key)
{
  for(UI_Box *p = ui_top_parent(); !ui_box_is_nil(p); p = p->parent)
  {
    if(p->flags & UI_BoxFlag_DefaultFocusNav)
    {
      p->default_nav_focus_next_active_key = key;
      break;
    }
  }
}

internal void
ui_set_auto_focus_hot_key(UI_Key key)
{
  for(UI_Box *p = ui_top_parent(); !ui_box_is_nil(p); p = p->parent)
  {
    if(p->flags & UI_BoxFlag_DefaultFocusNav)
    {
      p->default_nav_focus_next_hot_key = key;
      break;
    }
  }
}

//- rjf: box node construction

internal UI_Box *
ui_build_box_from_key(UI_BoxFlags flags, UI_Key key)
{
  ProfBeginFunction();
  ui_state->build_box_count += 1;
  
  //- rjf: grab active parent
  UI_Box *parent = ui_top_parent();
  
  //- rjf: try to get box
  UI_BoxFlags last_flags = 0;
  UI_Box *box = ui_box_from_key(key);
  B32 box_first_frame = ui_box_is_nil(box);
  last_flags = box->flags;
  
  //- rjf: zero key on duplicate
  if(!box_first_frame && box->last_touched_build_index == ui_state->build_index)
  {
    box = &ui_g_nil_box;
    key = ui_key_zero();
    box_first_frame = 1;
  }
  
  //- rjf: gather info from box
  B32 box_is_transient = ui_key_match(key, ui_key_zero());
  
  //- rjf: allocate box if it doesn't yet exist
  if(box_first_frame)
  {
    box = !box_is_transient ? ui_state->first_free_box : 0;
    ui_state->is_animating = ui_state->is_animating || !box_is_transient;
    if(!ui_box_is_nil(box))
    {
      SLLStackPop(ui_state->first_free_box);
    }
    else
    {
      box = push_array_no_zero(box_is_transient ? ui_build_arena() : ui_state->arena, UI_Box, 1);
    }
    MemoryZeroStruct(box);
  }
  
  //- rjf: zero out per-frame state
  {
    box->first = box->last = box->next = box->prev = box->parent = &ui_g_nil_box;
    box->child_count = 0;
    box->flags = 0;
    box->hover_cursor = OS_Cursor_Pointer;
    MemoryZeroArray(box->pref_size);
    MemoryZeroStruct(&box->draw_bucket);
  }
  
  //- rjf: hook into persistent state table
  if(box_first_frame && !box_is_transient)
  {
    U64 slot = key.u64[0] % ui_state->box_table_size;
    DLLInsert_NPZ(&ui_g_nil_box, ui_state->box_table[slot].hash_first, ui_state->box_table[slot].hash_last, ui_state->box_table[slot].hash_last, box, hash_next, hash_prev);
  }
  
  //- rjf: hook into per-frame tree structure
  if(!ui_box_is_nil(parent))
  {
    DLLPushBack_NPZ(&ui_g_nil_box, parent->first, parent->last, box, next, prev);
    parent->child_count += 1;
    box->parent = parent;
  }
  
  //- rjf: fill box
  {
    box->key = key;
    box->flags = flags|ui_state->flags_stack.top->v;
    box->fastpath_codepoint = ui_state->fastpath_codepoint_stack.top->v;
    
    if(ui_is_focus_active() && (box->flags & UI_BoxFlag_DefaultFocusNav) && ui_key_match(ui_state->default_nav_root_key, ui_key_zero()))
    {
      ui_state->default_nav_root_key = box->key;
    }
    
    if(box_first_frame)
    {
      box->first_touched_build_index = ui_state->build_index;
      box->disabled_t = (F32)!!(box->flags & UI_BoxFlag_Disabled);
    }
    box->last_touched_build_index = ui_state->build_index;
    
    if(box->flags & UI_BoxFlag_Disabled && (!(last_flags & UI_BoxFlag_Disabled) || box_first_frame))
    {
      box->first_disabled_build_index = ui_state->build_index;
    }
    
    if(ui_state->fixed_x_stack.top != &ui_state->fixed_x_nil_stack_top)
    {
      box->flags |= UI_BoxFlag_FloatingX;
      box->fixed_position.x = ui_state->fixed_x_stack.top->v;
    }
    if(ui_state->fixed_y_stack.top != &ui_state->fixed_y_nil_stack_top)
    {
      box->flags |= UI_BoxFlag_FloatingY;
      box->fixed_position.y = ui_state->fixed_y_stack.top->v;
    }
    if(ui_state->fixed_width_stack.top != &ui_state->fixed_width_nil_stack_top)
    {
      box->flags |= UI_BoxFlag_FixedWidth;
      box->fixed_size.x = ui_state->fixed_width_stack.top->v;
    }
    else
    {
      box->pref_size[Axis2_X] = ui_state->pref_width_stack.top->v;
    }
    if(ui_state->fixed_height_stack.top != &ui_state->fixed_height_nil_stack_top)
    {
      box->flags |= UI_BoxFlag_FixedHeight;
      box->fixed_size.y = ui_state->fixed_height_stack.top->v;
    }
    else
    {
      box->pref_size[Axis2_Y] = ui_state->pref_height_stack.top->v;
    }
    
    B32 is_auto_focus_active = ui_is_key_auto_focus_active(key);
    B32 is_auto_focus_hot    = ui_is_key_auto_focus_hot(key);
    if(is_auto_focus_active)
    {
      ui_set_next_focus_active(UI_FocusKind_On);
    }
    if(is_auto_focus_hot)
    {
      ui_set_next_focus_hot(UI_FocusKind_On);
    }
    box->flags |= UI_BoxFlag_FocusHot    * (ui_state->focus_hot_stack.top->v == UI_FocusKind_On);
    box->flags |= UI_BoxFlag_FocusActive * (ui_state->focus_active_stack.top->v == UI_FocusKind_On);
    if(box->flags & UI_BoxFlag_FocusHot && !ui_is_focus_hot())
    {
      box->flags |= UI_BoxFlag_FocusHotDisabled;
    }
    if(box->flags & UI_BoxFlag_FocusActive && !ui_is_focus_active())
    {
      box->flags |= UI_BoxFlag_FocusActiveDisabled;
    }
    
    box->text_align = ui_state->text_alignment_stack.top->v;
    box->child_layout_axis = ui_state->child_layout_axis_stack.top->v;
    box->background_color = ui_state->background_color_stack.top->v;
    box->text_color = ui_state->text_color_stack.top->v;
    box->border_color = ui_state->border_color_stack.top->v;
    box->overlay_color = ui_state->overlay_color_stack.top->v;
    box->font = ui_state->font_stack.top->v;
    box->font_size = ui_state->font_size_stack.top->v;
    box->corner_radii[Corner_00] = ui_state->corner_radius_00_stack.top->v;
    box->corner_radii[Corner_01] = ui_state->corner_radius_01_stack.top->v;
    box->corner_radii[Corner_10] = ui_state->corner_radius_10_stack.top->v;
    box->corner_radii[Corner_11] = ui_state->corner_radius_11_stack.top->v;
    box->blur_size = ui_state->blur_size_stack.top->v;
    box->transparency = ui_state->transparency_stack.top->v;
    box->squish = ui_state->squish_stack.top->v;
    box->text_padding = ui_state->text_padding_stack.top->v;
    box->hover_cursor = ui_state->hover_cursor_stack.top->v;
    box->custom_draw = 0;
  }
  
  //- rjf: auto-pop all stacks
  {
    UI_AutoPopStacks(ui_state);
  }
  
  //- rjf: return
  ProfEnd();
  return box;
}

internal UI_Key
ui_active_seed_key(void)
{
  UI_Box *keyed_ancestor = &ui_g_nil_box;
  {
    for(UI_Box *p = ui_top_parent(); !ui_box_is_nil(p); p = p->parent)
    {
      if(!ui_key_match(ui_key_zero(), p->key))
      {
        keyed_ancestor = p;
        break;
      }
    }
  }
  return keyed_ancestor->key;
}

internal UI_Box *
ui_build_box_from_string(UI_BoxFlags flags, String8 string)
{
  ProfBeginFunction();
  
  //- rjf: grab active parent
  UI_Box *parent = ui_top_parent();
  
  //- rjf: figure out key
  UI_Key key = ui_key_from_string(ui_active_seed_key(), string);
  
  //- rjf: build box from key, equip passed string
  UI_Box *box = ui_build_box_from_key(flags, key);
  if(flags & UI_BoxFlag_DrawText)
  {
    ui_box_equip_display_string(box, string);
  }
  
  //- rjf: return
  ProfEnd();
  return box;
}

internal UI_Box *
ui_build_box_from_stringf(UI_BoxFlags flags, char *fmt, ...)
{
  Temp scratch = scratch_begin(0, 0);
  va_list args;
  va_start(args, fmt);
  String8 string = push_str8fv(scratch.arena, fmt, args);
  va_end(args);
  UI_Box *box = ui_build_box_from_string(flags, string);
  scratch_end(scratch);
  return box;
}

//- rjf: box node equipment

internal void
ui_box_equip_display_string(UI_Box *box, String8 string)
{
  ProfBeginFunction();
  box->string = push_str8_copy(ui_build_arena(), string);
  box->flags |= UI_BoxFlag_HasDisplayString;
  if(box->flags & UI_BoxFlag_DrawText && (box->fastpath_codepoint == 0 || !(box->flags & UI_BoxFlag_DrawTextFastpathCodepoint)))
  {
    String8 display_string = ui_box_display_string(box);
    D_FancyStringNode fancy_string_n = {0, {box->font, display_string, box->text_color, box->font_size, 0, 0}};
    D_FancyStringList fancy_strings = {&fancy_string_n, &fancy_string_n, 1};
    box->display_string_runs = d_fancy_run_list_from_fancy_string_list(ui_build_arena(), &fancy_strings);
  }
  else if(box->flags & UI_BoxFlag_DrawText && box->flags & UI_BoxFlag_DrawTextFastpathCodepoint && box->fastpath_codepoint != 0)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 display_string = ui_box_display_string(box);
    String32 fpcp32 = str32(&box->fastpath_codepoint, 1);
    String8 fpcp = str8_from_32(scratch.arena, fpcp32);
    U64 fpcp_pos = str8_find_needle(display_string, 0, fpcp, StringMatchFlag_CaseInsensitive);
    if(fpcp_pos < display_string.size)
    {
      D_FancyStringNode pst_fancy_string_n = {0,                   {box->font, str8_skip(display_string, fpcp_pos+fpcp.size), box->text_color, box->font_size, 0, 0}};
      D_FancyStringNode cdp_fancy_string_n = {&pst_fancy_string_n, {box->font, str8_substr(display_string, r1u64(fpcp_pos, fpcp_pos+fpcp.size)), box->text_color, box->font_size, 4.f, 0}};
      D_FancyStringNode pre_fancy_string_n = {&cdp_fancy_string_n, {box->font, str8_prefix(display_string, fpcp_pos), box->text_color, box->font_size, 0, 0}};
      D_FancyStringList fancy_strings = {&pre_fancy_string_n, &pst_fancy_string_n, 3};
      box->display_string_runs = d_fancy_run_list_from_fancy_string_list(ui_build_arena(), &fancy_strings);
    }
    else
    {
      D_FancyStringNode fancy_string_n = {0, {box->font, display_string, box->text_color, box->font_size, 0, 0}};
      D_FancyStringList fancy_strings = {&fancy_string_n, &fancy_string_n, 1};
      box->display_string_runs = d_fancy_run_list_from_fancy_string_list(ui_build_arena(), &fancy_strings);
    }
    scratch_end(scratch);
  }
  ProfEnd();
}

internal void
ui_box_equip_display_fancy_strings(UI_Box *box, D_FancyStringList *strings)
{
  box->flags |= UI_BoxFlag_HasDisplayString;
  box->string = d_string_from_fancy_string_list(ui_build_arena(), strings);
  box->display_string_runs = d_fancy_run_list_from_fancy_string_list(ui_build_arena(), strings);
}

internal inline void
ui_box_equip_display_string_fancy_runs(UI_Box *box, String8 string, D_FancyRunList *runs)
{
  box->flags |= UI_BoxFlag_HasDisplayString;
  box->string = push_str8_copy(ui_build_arena(), string);
  box->display_string_runs = d_fancy_run_list_copy(ui_build_arena(), runs);
}

internal inline void
ui_box_equip_fuzzy_match_ranges(UI_Box *box, FuzzyMatchRangeList *matches)
{
  box->flags |= UI_BoxFlag_HasFuzzyMatchRanges;
  box->fuzzy_match_ranges = fuzzy_match_range_list_copy(ui_build_arena(), matches);
}

internal void
ui_box_equip_draw_bucket(UI_Box *box, D_Bucket *bucket)
{
  box->flags |= UI_BoxFlag_DrawBucket;
  if(box->draw_bucket != 0)
  {
    D_BucketScope(box->draw_bucket) d_sub_bucket(bucket);
  }
  else
  {
    box->draw_bucket = bucket;
  }
}

internal void
ui_box_equip_custom_draw(UI_Box *box, UI_BoxCustomDrawFunctionType *custom_draw, void *user_data)
{
  box->custom_draw = custom_draw;
  box->custom_draw_user_data = user_data;
}

//- rjf: box accessors / queries

internal String8
ui_box_display_string(UI_Box *box)
{
  String8 result = box->string;
  if(!(box->flags & UI_BoxFlag_DisableIDString))
  {
    result = ui_display_part_from_key_string(result);
  }
  return result;
}

internal Vec2F32
ui_box_text_position(UI_Box *box)
{
  Vec2F32 result = {0};
  F_Tag font = box->font;
  F32 font_size = box->font_size;
  F_Metrics font_metrics = f_metrics_from_tag_size(font, font_size);
  result.y = ceil_f32((box->rect.p0.y + box->rect.p1.y)/2.f + (font_metrics.capital_height/2) - font_metrics.line_gap/2);
  switch(box->text_align)
  {
    default:
    case UI_TextAlign_Left:
    {
      result.x = box->rect.p0.x + 2.f + box->text_padding;
    }break;
    case UI_TextAlign_Center:
    {
      Vec2F32 advance = box->display_string_runs.dim;
      result.x = (box->rect.p0.x + box->rect.p1.x)/2 - advance.x/2;
      result.x = ClampBot(result.x, box->rect.x0);
    }break;
    case UI_TextAlign_Right:
    {
      Vec2F32 advance = box->display_string_runs.dim;
      result.x = (box->rect.p1.x) - 1.f - advance.x;
      result.x = ClampBot(result.x, box->rect.x0);
    }break;
  }
  result.x = floorf(result.x);
  return result;
}

internal U64
ui_box_char_pos_from_xy(UI_Box *box, Vec2F32 xy)
{
  F_Tag font = box->font;
  F32 font_size = box->font_size;
  String8 line = ui_box_display_string(box);
  U64 result = f_char_pos_from_tag_size_string_p(font, font_size, line, xy.x - ui_box_text_position(box).x);
  return result;
}

////////////////////////////////
//~ rjf: Box Interaction

//- rjf: single-line string editing

internal B32
ui_do_single_line_string_edits(TxtPt *cursor, TxtPt *mark, U64 string_max, String8 *out_string)
{
  B32 change = 0;
  Temp scratch = scratch_begin(0, 0);
  UI_NavActionList *nav_actions = ui_nav_actions();
  for(UI_NavActionNode *n = nav_actions->first, *next = 0; n != 0; n = next)
  {
    next = n->next;
    
    // rjf: do not consume anything that doesn't fit a single-line's operations
    if(n->v.delta.y != 0)
    {
      continue;
    }
    
    // rjf: map this action to an op
    B32 taken = 0;
    UI_NavTxtOp op = ui_nav_single_line_txt_op_from_action(scratch.arena, n->v, *out_string, *cursor, *mark);
    
    // rjf: perform replace range
    if(!txt_pt_match(op.range.min, op.range.max) || op.replace.size != 0)
    {
      taken = 1;
      String8 new_string = ui_nav_push_string_replace_range(scratch.arena, *out_string, r1s64(op.range.min.column, op.range.max.column), op.replace);
      new_string.size = Min(string_max, new_string.size);
      MemoryCopy(out_string->str, new_string.str, new_string.size);
      out_string->size = new_string.size;
    }
    
    // rjf: perform copy
    if(op.flags & UI_NavTxtOpFlag_Copy)
    {
      taken = 1;
      os_set_clipboard_text(op.copy);
    }
    
    // rjf: commit op's changed cursor & mark to caller-provided state
    taken = taken || (!txt_pt_match(*cursor, op.cursor) || !txt_pt_match(*mark, op.mark));
    *cursor = op.cursor;
    *mark = op.mark;
    
    // rjf: consume event
    if(taken)
    {
      ui_nav_eat_action_node(nav_actions, n);
      change = 1;
    }
  }
  scratch_end(scratch);
  return change;
}

//- rjf: general box interaction path

internal UI_Signal
ui_signal_from_box(UI_Box *box)
{
  ProfBeginFunction();
  B32 is_focus_hot = box->flags & UI_BoxFlag_FocusHot && !(box->flags & UI_BoxFlag_FocusHotDisabled);
  UI_Signal sig = {box};
  sig.event_flags |= os_get_event_flags();
  
  //////////////////////////////
  //- rjf: calculate possibly-clipped box rectangle
  //
  Rng2F32 rect = box->rect;
  for(UI_Box *b = box->parent; !ui_box_is_nil(b); b = b->parent)
  {
    if(b->flags & UI_BoxFlag_Clip)
    {
      rect = intersect_2f32(rect, b->rect);
    }
  }
  
  //////////////////////////////
  //- rjf: determine if we're under the context menu or not
  //
  B32 ctx_menu_is_ancestor = 0;
  ProfScope("check context menu ancestor")
  {
    for(UI_Box *parent = box; !ui_box_is_nil(parent); parent = parent->parent)
    {
      if(parent == ui_state->ctx_menu_root)
      {
        ctx_menu_is_ancestor = 1;
        break;
      }
    }
  }
  
  //////////////////////////////
  //- rjf: calculate blacklist rectangles
  //
  Rng2F32 blacklist_rect = {0};
  if(!ctx_menu_is_ancestor && ui_state->ctx_menu_open)
  {
    blacklist_rect = ui_state->ctx_menu_root->rect;
  }
  
  //////////////////////////////
  //- rjf: process events related to this box
  //
  B32 view_scrolled = 0;
  for(OS_Event *evt = ui_state->events->first, *next = 0;
      evt != 0;
      evt = next)
  {
    B32 taken = 0;
    next = evt->next;
    
    //- rjf: skip disqualified events
    if(!os_handle_match(evt->window, ui_state->window)) {continue;}
    
    //- rjf: unpack event
    Vec2F32 evt_mouse = evt->pos;
    B32 evt_mouse_in_bounds = !contains_2f32(blacklist_rect, evt_mouse) && contains_2f32(rect, evt_mouse);
    UI_MouseButtonKind evt_mouse_button_kind = (evt->key == OS_Key_LeftMouseButton   ? UI_MouseButtonKind_Left :
                                                evt->key == OS_Key_MiddleMouseButton ? UI_MouseButtonKind_Middle :
                                                evt->key == OS_Key_RightMouseButton  ? UI_MouseButtonKind_Right :
                                                UI_MouseButtonKind_Left);
    B32 evt_key_is_mouse = (evt->key == OS_Key_LeftMouseButton ||
                            evt->key == OS_Key_MiddleMouseButton ||
                            evt->key == OS_Key_RightMouseButton);
    sig.event_flags |= evt->flags;
    
    //- rjf: mouse presses in box -> set hot/active; mark signal accordingly
    if(box->flags & UI_BoxFlag_MouseClickable &&
       evt->kind == OS_EventKind_Press &&
       evt_mouse_in_bounds &&
       evt_key_is_mouse)
    {
      ui_state->hot_box_key = box->key;
      ui_state->active_box_key[evt_mouse_button_kind] = box->key;
      sig.f |= (UI_SignalFlag_LeftPressed<<evt_mouse_button_kind);
      ui_state->drag_start_mouse = evt->pos;
      if(ui_key_match(box->key, ui_state->last_press_key[evt_mouse_button_kind]) &&
         evt->timestamp_us-ui_state->last_press_timestamp_us[evt_mouse_button_kind] <= 1000000*os_double_click_time())
      {
        sig.f |= (UI_SignalFlag_LeftDoubleClicked<<evt_mouse_button_kind);
      }
      ui_state->last_press_key[evt_mouse_button_kind] = box->key;
      ui_state->last_press_timestamp_us[evt_mouse_button_kind] = evt->timestamp_us;
      taken = 1;
    }
    
    //- rjf: mouse releases in active box -> unset active; mark signal accordingly
    if(box->flags & UI_BoxFlag_MouseClickable &&
       evt->kind == OS_EventKind_Release &&
       ui_key_match(ui_state->active_box_key[evt_mouse_button_kind], box->key) &&
       evt_mouse_in_bounds &&
       evt_key_is_mouse)
    {
      ui_state->active_box_key[evt_mouse_button_kind] = ui_key_zero();
      sig.f |= (UI_SignalFlag_LeftReleased<<evt_mouse_button_kind);
      sig.f |= (UI_SignalFlag_LeftClicked<<evt_mouse_button_kind);
      taken = 1;
    }
    
    //- rjf: mouse releases outside active box -> unset hot/active
    if(box->flags & UI_BoxFlag_MouseClickable &&
       evt->kind == OS_EventKind_Release &&
       ui_key_match(ui_state->active_box_key[evt_mouse_button_kind], box->key) &&
       !evt_mouse_in_bounds &&
       evt_key_is_mouse)
    {
      ui_state->hot_box_key = ui_key_zero();
      ui_state->active_box_key[evt_mouse_button_kind] = ui_key_zero();
      sig.f |= (UI_SignalFlag_LeftReleased<<evt_mouse_button_kind);
      taken = 1;
    }
    
    //- rjf: focus is hot & keyboard click -> mark signal
    if(box->flags & UI_BoxFlag_KeyboardClickable &&
       is_focus_hot &&
       evt->kind == OS_EventKind_Press &&
       evt->key == OS_Key_Return)
    {
      sig.f |= UI_SignalFlag_KeyboardPressed;
      taken = 1;
    }
    
    //- rjf: scrolling
    if(box->flags & UI_BoxFlag_Scroll &&
       evt->kind == OS_EventKind_Scroll &&
       evt->flags != OS_EventFlag_Ctrl &&
       evt_mouse_in_bounds)
    {
      Vec2F32 delta = evt->delta;
      if(evt->flags & OS_EventFlag_Shift)
      {
        Swap(F32, delta.x, delta.y);
      }
      sig.scroll.x += (S16)(delta.x/30.f);
      sig.scroll.y += (S16)(delta.y/30.f);
      taken = 1;
    }
    
    //- rjf: view scrolling
    if(box->flags & UI_BoxFlag_ViewScroll && box->first_touched_build_index != box->last_touched_build_index &&
       evt->kind == OS_EventKind_Scroll &&
       evt->flags != OS_EventFlag_Ctrl &&
       evt_mouse_in_bounds)
    {
      Vec2F32 delta = evt->delta;
      if(evt->flags & OS_EventFlag_Shift)
      {
        Swap(F32, delta.x, delta.y);
      }
      if(!(box->flags & UI_BoxFlag_ViewScrollX))
      {
        delta.x = 0;
      }
      if(!(box->flags & UI_BoxFlag_ViewScrollY))
      {
        delta.y = 0;
      }
      os_eat_event(ui_state->events, evt);
      box->view_off_target.x += delta.x;
      box->view_off_target.y += delta.y;
      view_scrolled = 1;
      taken = 1;
    }
    
    //- rjf: taken -> eat event
    if(taken)
    {
      os_eat_event(ui_state->events, evt);
    }
  }
  
  //////////////////////////////
  //- rjf: process nav actions related to this box
  //
  {
    for(UI_NavActionNode *n = ui_state->nav_actions->first, *next = 0;
        n != 0;
        n = next)
    {
      next = n->next;
      UI_NavAction *action = &n->v;
      B32 taken = 0;
      if(is_focus_hot && box->flags & UI_BoxFlag_KeyboardClickable && action->flags & UI_NavActionFlag_Copy)
      {
        ui_state->clipboard_copy_key = box->key;
        taken = 1;
      }
      if(box->flags & UI_BoxFlag_Clickable && box->fastpath_codepoint != 0)
      {
        B32 ancestor_is_focused = 0;
        for(UI_Box *parent = box->parent; !ui_box_is_nil(parent); parent = parent->parent)
        {
          if(parent->flags & UI_BoxFlag_FocusActive)
          {
            ancestor_is_focused = 1;
            if(parent->flags & UI_BoxFlag_FocusActiveDisabled ||
               !ui_key_match(parent->default_nav_focus_active_key, ui_key_zero()))
            {
              ancestor_is_focused = 0;
              break;
            }
          }
        }
        if(ancestor_is_focused && action->insertion.size != 0)
        {
          Temp scratch = scratch_begin(0, 0);
          String32 insertion32 = str32_from_8(scratch.arena, action->insertion);
          if(insertion32.size == 1 && insertion32.str[0] == box->fastpath_codepoint)
          {
            taken = 1;
            sig.f |= UI_SignalFlag_Clicked|UI_SignalFlag_Pressed;
          }
          scratch_end(scratch);
        }
      }
      if(taken)
      {
        ui_nav_eat_action_node(ui_nav_actions(), n);
      }
    }
  }
  
  //////////////////////////////
  //- rjf: clamp view scrolling
  //
  if(view_scrolled && box->flags & UI_BoxFlag_ViewClamp)
  {
    Vec2F32 max_view_off_target =
    {
      ClampBot(0, box->view_bounds.x - box->fixed_size.x),
      ClampBot(0, box->view_bounds.y - box->fixed_size.y),
    };
    if(box->flags & UI_BoxFlag_ViewClampX) { box->view_off_target.x = Clamp(0, box->view_off_target.x, max_view_off_target.x); }
    if(box->flags & UI_BoxFlag_ViewClampY) { box->view_off_target.y = Clamp(0, box->view_off_target.y, max_view_off_target.y); }
  }
  
  //////////////////////////////
  //- rjf: active -> dragging
  //
  for(EachEnumVal(UI_MouseButtonKind, k))
  {
    if(ui_key_match(ui_state->active_box_key[k], box->key) ||
       sig.f & (UI_SignalFlag_LeftPressed<<k))
    {
      sig.f |= (UI_SignalFlag_LeftDragging<<k);
    }
  }
  
  //////////////////////////////
  //- rjf: mouse is over this box's rect -> always mark mouse-over
  //
  {
    if(contains_2f32(rect, ui_state->mouse) &&
       !contains_2f32(blacklist_rect, ui_state->mouse))
    {
      sig.f |= UI_SignalFlag_MouseOver;
    }
  }
  
  //////////////////////////////
  //- rjf: mouse is over this box's rect, no other hot key? -> set hot key, mark hovering
  //
  {
    if(contains_2f32(rect, ui_state->mouse) &&
       !contains_2f32(blacklist_rect, ui_state->mouse) &&
       (ui_key_match(ui_state->hot_box_key, ui_key_zero()) || ui_key_match(ui_state->hot_box_key, box->key)) &&
       (ui_key_match(ui_state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()) || ui_key_match(ui_state->active_box_key[UI_MouseButtonKind_Left], box->key)) &&
       (ui_key_match(ui_state->active_box_key[UI_MouseButtonKind_Middle], ui_key_zero()) || ui_key_match(ui_state->active_box_key[UI_MouseButtonKind_Middle], box->key)) &&
       (ui_key_match(ui_state->active_box_key[UI_MouseButtonKind_Right], ui_key_zero()) || ui_key_match(ui_state->active_box_key[UI_MouseButtonKind_Right], box->key)))
    {
      ui_state->hot_box_key = box->key;
      sig.f |= UI_SignalFlag_Hovering;
    }
  }
  
  //////////////////////////////
  //- rjf: clicking on something outside the context menu kills the context menu
  //
  if(!ctx_menu_is_ancestor && sig.f & (UI_SignalFlag_LeftPressed|UI_SignalFlag_RightPressed|UI_SignalFlag_MiddlePressed))
  {
    ui_ctx_menu_close();
  }
  
  //////////////////////////////
  //- rjf: get default nav ancestor
  //
  UI_Box *default_nav_parent = &ui_g_nil_box;
  for(UI_Box *p = ui_top_parent(); !ui_box_is_nil(p); p = p->parent)
  {
    if(p->flags & UI_BoxFlag_DefaultFocusNav)
    {
      default_nav_parent = p;
      break;
    }
  }
  
  //////////////////////////////
  //- rjf: clicking in default nav -> set navigation state to this box
  //
  if(box->flags & UI_BoxFlag_ClickToFocus && sig.f&UI_SignalFlag_Pressed && !ui_box_is_nil(default_nav_parent))
  {
    default_nav_parent->default_nav_focus_next_hot_key = box->key;
    if(!ui_key_match(default_nav_parent->default_nav_focus_active_key, box->key))
    {
      default_nav_parent->default_nav_focus_next_active_key = ui_key_zero();
    }
  }
  
  
  ProfEnd();
  return sig;
}

////////////////////////////////
//~ rjf: Stacks

//- rjf: helpers

internal Rng2F32
ui_push_rect(Rng2F32 rect)
{
  Rng2F32 replaced = {0};
  Vec2F32 size = dim_2f32(rect);
  replaced.x0 = ui_push_fixed_x(rect.x0);
  replaced.y0 = ui_push_fixed_y(rect.y0);
  replaced.x1 = replaced.x0 + ui_push_fixed_width(size.x);
  replaced.y1 = replaced.y0 + ui_push_fixed_height(size.y);
  return replaced;
}

internal Rng2F32
ui_pop_rect(void)
{
  Rng2F32 popped = {0};
  popped.x0 = ui_pop_fixed_x();
  popped.y0 = ui_pop_fixed_y();
  popped.x1 = popped.x0 + ui_pop_fixed_width();
  popped.y1 = popped.y0 + ui_pop_fixed_height();
  return popped;
}

internal UI_Size
ui_push_pref_size(Axis2 axis, UI_Size v)
{
  UI_Size result = zero_struct;
  switch(axis)
  {
    default: break;
    case Axis2_X: {result = ui_push_pref_width(v);}break;
    case Axis2_Y: {result = ui_push_pref_height(v);}break;
  }
  return result;
}

internal UI_Size
ui_pop_pref_size(Axis2 axis)
{
  UI_Size result = zero_struct;
  switch(axis)
  {
    default: break;
    case Axis2_X: {result = ui_pop_pref_width();}break;
    case Axis2_Y: {result = ui_pop_pref_height();}break;
  }
  return result;
}

internal UI_Size
ui_set_next_pref_size(Axis2 axis, UI_Size v)
{
  return (axis == Axis2_X ? ui_set_next_pref_width : ui_set_next_pref_height)(v);
}

internal void
ui_push_corner_radius(F32 v)
{
  ui_push_corner_radius_00(v);
  ui_push_corner_radius_01(v);
  ui_push_corner_radius_10(v);
  ui_push_corner_radius_11(v);
}

internal void
ui_pop_corner_radius(void)
{
  ui_pop_corner_radius_00();
  ui_pop_corner_radius_01();
  ui_pop_corner_radius_10();
  ui_pop_corner_radius_11();
}

////////////////////////////////
//~ rjf: Generated Code

#define UI_StackTopImpl(state, name_upper, name_lower) \
return state->name_lower##_stack.top->v;

#define UI_StackBottomImpl(state, name_upper, name_lower) \
return state->name_lower##_stack.bottom_val;

#define UI_StackPushImpl(state, name_upper, name_lower, type, new_value) \
UI_##name_upper##Node *node = state->name_lower##_stack.free;\
if(node != 0) {SLLStackPop(state->name_lower##_stack.free);}\
else {node = push_array(ui_build_arena(), UI_##name_upper##Node, 1);}\
type old_value = state->name_lower##_stack.top->v;\
node->v = new_value;\
SLLStackPush(state->name_lower##_stack.top, node);\
if(node->next == &state->name_lower##_nil_stack_top)\
{\
state->name_lower##_stack.bottom_val = (new_value);\
}\
state->name_lower##_stack.auto_pop = 0;\
return old_value;

#define UI_StackPopImpl(state, name_upper, name_lower) \
UI_##name_upper##Node *popped = state->name_lower##_stack.top;\
if(popped != &state->name_lower##_nil_stack_top)\
{\
SLLStackPop(state->name_lower##_stack.top);\
SLLStackPush(state->name_lower##_stack.free, popped);\
state->name_lower##_stack.auto_pop = 0;\
}\
return popped->v;\

#define UI_StackSetNextImpl(state, name_upper, name_lower, type, new_value) \
UI_##name_upper##Node *node = state->name_lower##_stack.free;\
if(node != 0) {SLLStackPop(state->name_lower##_stack.free);}\
else {node = push_array(ui_build_arena(), UI_##name_upper##Node, 1);}\
type old_value = state->name_lower##_stack.top->v;\
node->v = new_value;\
SLLStackPush(state->name_lower##_stack.top, node);\
state->name_lower##_stack.auto_pop = 1;\
return old_value;

#include "generated/ui.meta.c"
