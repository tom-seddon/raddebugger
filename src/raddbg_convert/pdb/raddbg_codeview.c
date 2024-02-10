// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ CodeView Common Functions

static CV_NumericParsed
cv_numeric_from_data_range(U8 *first, U8 *opl){
  CV_NumericParsed result = {0};
  if (first + 2 <= opl){
    U16 x = *(U16*)first;
    if (x < 0x8000){
      result.kind = CV_NumericKind_USHORT;
      result.val = first;
      result.encoded_size = 2;
    }
    else{
      U64 val_size = 0;
      switch (x){
        case CV_NumericKind_CHAR:      val_size = 1; break;
        case CV_NumericKind_SHORT:
        case CV_NumericKind_USHORT:    val_size = 2; break;
        case CV_NumericKind_LONG:
        case CV_NumericKind_ULONG:     val_size = 4; break;
        case CV_NumericKind_FLOAT32:   val_size = 4; break;
        case CV_NumericKind_FLOAT64:   val_size = 8; break;
        case CV_NumericKind_FLOAT80:   val_size = 10; break;
        case CV_NumericKind_FLOAT128:  val_size = 16; break;
        case CV_NumericKind_QUADWORD:
        case CV_NumericKind_UQUADWORD: val_size = 8; break;
        case CV_NumericKind_FLOAT48:   val_size = 6; break;
        case CV_NumericKind_COMPLEX32: val_size = 8; break;
        case CV_NumericKind_COMPLEX64: val_size = 16; break;
        case CV_NumericKind_COMPLEX80: val_size = 20; break;
        case CV_NumericKind_COMPLEX128:val_size = 32; break;
        case CV_NumericKind_VARSTRING: val_size = 0; break; // TODO: ???
        case CV_NumericKind_OCTWORD:
        case CV_NumericKind_UOCTWORD:  val_size = 16; break;
        case CV_NumericKind_DECIMAL:   val_size = 0; break; // TODO: ???
        case CV_NumericKind_DATE:      val_size = 0; break; // TODO: ???
        case CV_NumericKind_UTF8STRING:val_size = 0; break; // TODO: ???
        case CV_NumericKind_FLOAT16:   val_size = 2; break;
      }
      
      if (first + 2 + val_size <= opl){
        result.kind = x;
        result.val = (first + 2);
        result.encoded_size = 2 + val_size;
      }
    }
  }
  return(result);
}

static B32
cv_numeric_fits_in_u64(CV_NumericParsed *num){
  B32 result = 0;
  switch (num->kind){
    case CV_NumericKind_USHORT:
    case CV_NumericKind_ULONG:
    case CV_NumericKind_UQUADWORD:
    {
      result = 1;
    }break;
  }
  return(result);
}

static B32
cv_numeric_fits_in_s64(CV_NumericParsed *num){
  B32 result = 0;
  switch (num->kind){
    case CV_NumericKind_CHAR:
    case CV_NumericKind_SHORT:
    case CV_NumericKind_LONG:
    case CV_NumericKind_QUADWORD:
    {
      result = 1;
    }break;
  }
  return(result);
}

static B32
cv_numeric_fits_in_f64(CV_NumericParsed *num){
  B32 result = 0;
  switch (num->kind){
    case CV_NumericKind_FLOAT32:
    case CV_NumericKind_FLOAT64:
    {
      result = 1;
    }break;
  }
  return(result);
}

static U64
cv_u64_from_numeric(CV_NumericParsed *num){
  U64 result = 0;
  switch (num->kind){
    case CV_NumericKind_USHORT:
    {
      result = *(U16*)num->val;
    }break;
    
    case CV_NumericKind_ULONG:
    {
      result = *(U32*)num->val;
    }break;
    
    case CV_NumericKind_UQUADWORD:
    {
      result = *(U64*)num->val;
    }break;
  }
  return(result);
}

static S64
cv_s64_from_numeric(CV_NumericParsed *num){
  S64 result = 0;
  switch (num->kind){
    case CV_NumericKind_CHAR:
    {
      result = *(S8*)num->val;
    }break;
    
    case CV_NumericKind_SHORT:
    {
      result = *(S16*)num->val;
    }break;
    
    case CV_NumericKind_LONG:
    {
      result = *(S32*)num->val;
    }break;
    
    case CV_NumericKind_QUADWORD:
    {
      result = *(S64*)num->val;
    }break;
  }
  return(result);
}

static F64
cv_f64_from_numeric(CV_NumericParsed *num){
  F64 result = 0;
  switch (num->kind){
    case CV_NumericKind_FLOAT32:
    {
      result = *(F32*)num->val;
    }break;
    
    case CV_NumericKind_FLOAT64:
    {
      result = *(F64*)num->val;
    }break;
  }
  return(result);
}


////////////////////////////////
//~ CodeView Sym Parser Functions

static CV_SymParsed*
cv_sym_from_data(Arena *arena, String8 sym_data, U64 sym_align){
  Assert(1 <= sym_align && IsPow2OrZero(sym_align));
  ProfBegin("cv_sym_from_data");
  
  Temp scratch = scratch_begin(&arena, 1);
  
  // gather up symbols
  CV_RecRangeStream *stream = cv_rec_range_stream_from_data(scratch.arena, sym_data, sym_align);
  
  // convert to result
  CV_SymParsed *result = push_array(arena, CV_SymParsed, 1);
  result->data = sym_data;
  result->sym_align = sym_align;
  result->sym_ranges = cv_rec_range_array_from_stream(arena, stream);
  cv_sym_top_level_info_from_syms(arena, sym_data, &result->sym_ranges, &result->info);
  
  scratch_end(scratch);
  
  ProfEnd();
  
  return(result);
}

static CV_LeafParsed*
cv_leaf_from_data(Arena *arena, String8 leaf_data, CV_TypeId itype_first){
  ProfBegin("cv_leaf_from_data");
  
  Temp scratch = scratch_begin(&arena, 1);
  
  // gather up symbols
  CV_RecRangeStream *stream = cv_rec_range_stream_from_data(scratch.arena, leaf_data, 1);
  
  // convert to result
  CV_LeafParsed *result = push_array(arena, CV_LeafParsed, 1);
  result->data = leaf_data;
  result->itype_first = itype_first;
  result->itype_opl = itype_first + stream->total_count;
  result->leaf_ranges = cv_rec_range_array_from_stream(arena, stream);
  
  scratch_end(scratch);
  
  ProfEnd();
  
  return(result);
}

static CV_RecRangeStream*
cv_rec_range_stream_from_data(Arena *arena, String8 sym_data, U64 sym_align){
  Assert(1 <= sym_align && IsPow2OrZero(sym_align));
  
  CV_RecRangeStream *result = push_array(arena, CV_RecRangeStream, 1);
  
  U8 *data = sym_data.str;
  U64 cursor = 0;
  U64 cap = sym_data.size;
  for (;cursor + sizeof(CV_RecHeader) <= cap;){
    // setup a new chunk
    arena_push_align(arena, 64);
    CV_RecRangeChunk *cur_chunk = cv_rec_range_stream_push_chunk(arena, result);
    
    U64 partial_count = 0;
    for (;partial_count < CV_REC_RANGE_CHUNK_SIZE && cursor + sizeof(CV_RecHeader) <= cap;
         partial_count += 1){
      // compute cap
      CV_RecHeader *hdr = (CV_RecHeader*)(data + cursor);
      U64 symbol_cap_unclamped = cursor + 2 + hdr->size;
      U64 symbol_cap = ClampTop(symbol_cap_unclamped, cap);
      
      // push on range
      cur_chunk->ranges[partial_count].off = cursor + 2;
      cur_chunk->ranges[partial_count].hdr = *hdr;
      
      // update cursor
      U32 next_pos = AlignPow2(symbol_cap, sym_align);
      cursor = next_pos;
    }
    
    result->total_count += partial_count;
  }
  
  return(result);
}

static void
cv_sym_top_level_info_from_syms(Arena *arena, String8 sym_data,
                                CV_RecRangeArray *ranges,
                                CV_SymTopLevelInfo *info_out){
  MemoryZeroStruct(info_out);
  
  CV_RecRange *range = ranges->ranges;
  CV_RecRange *opl = range + ranges->count;
  for (; range < opl; range += 1){
    U8 *first = sym_data.str + range->off + 2;
    U64 cap = range->hdr.size - 2;
    
    switch (range->hdr.kind){
      case CV_SymKind_COMPILE:
      {
        if (sizeof(CV_SymCompile) <= cap){
          CV_SymCompile *compile = (CV_SymCompile*)first;
          
          String8 ver_str = str8_cstring_capped((char*)(compile + 1), (char *)(first + cap));
          
          info_out->arch = compile->machine;
          info_out->language = CV_CompileFlags_ExtractLanguage(compile->flags);;
          info_out->compiler_name = ver_str;
        }
      }break;
      
      case CV_SymKind_COMPILE2:
      {
        if (sizeof(CV_SymCompile2) <= cap){
          CV_SymCompile2 *compile2 = (CV_SymCompile2*)first;
          
          String8 ver_str = str8_cstring_capped((char*)(compile2 + 1), (char*)(first + cap));
          String8 compiler_name = push_str8f(arena, "%.*s %u.%u.%u",
                                             str8_varg(ver_str),
                                             compile2->ver_major,
                                             compile2->ver_minor,
                                             compile2->ver_build);
          
          info_out->arch = compile2->machine;
          info_out->language = CV_Compile2Flags_ExtractLanguage(compile2->flags);;
          info_out->compiler_name = compiler_name;
        }
      }break;
      
      case CV_SymKind_COMPILE3:
      {
        if (sizeof(CV_SymCompile3) <= cap){
          CV_SymCompile3 *compile3 = (CV_SymCompile3*)first;
          
          String8 ver_str = str8_cstring_capped((char*)(compile3 + 1), (char *)(first + cap));
          String8 compiler_name = push_str8f(arena, "%.*s %u.%u.%u",
                                             str8_varg(ver_str),
                                             compile3->ver_major,
                                             compile3->ver_minor,
                                             compile3->ver_build);
          
          info_out->arch = compile3->machine;
          info_out->language = CV_Compile3Flags_ExtractLanguage(compile3->flags);;
          info_out->compiler_name = compiler_name;
        }
      }break;
    }
  }
}


//- range streams

static CV_RecRangeChunk*
cv_rec_range_stream_push_chunk(Arena *arena, CV_RecRangeStream *stream){
  CV_RecRangeChunk *result = push_array_no_zero(arena, CV_RecRangeChunk, 1);
  SLLQueuePush(stream->first_chunk, stream->last_chunk, result);
  return(result);
}

static CV_RecRangeArray
cv_rec_range_array_from_stream(Arena *arena, CV_RecRangeStream *stream){
  U64 total_count = stream->total_count;
  CV_RecRange *ranges = push_array_no_zero(arena, CV_RecRange, total_count);
  U64 idx = 0;
  for (CV_RecRangeChunk *chunk = stream->first_chunk;
       chunk != 0;
       chunk = chunk->next){
    U64 copy_count_raw = total_count - idx;
    U64 copy_count = ClampTop(copy_count_raw, CV_REC_RANGE_CHUNK_SIZE);
    MemoryCopy(ranges + idx, chunk->ranges, copy_count*sizeof(CV_RecRange));
    idx += copy_count;
  }
  CV_RecRangeArray result = {0};
  result.ranges = ranges;
  result.count = total_count;
  return(result);
}

////////////////////////////////
//~ CodeView C13 Parser Functions

static CV_C13Parsed*
cv_c13_from_data(Arena *arena, String8 c13_data,
                 PDB_Strtbl *strtbl, PDB_CoffSectionArray *sections){
  ProfBegin("cv_c13_from_data");
  
  // gather c13 data
  CV_C13SubSectionNode *file_chksms = 0;
  CV_C13SubSectionNode *first = 0;
  CV_C13SubSectionNode *last = 0;
  U64 count = 0;
  {
    U32 cursor = 0;
    for (; cursor + sizeof(CV_C13_SubSectionHeader) <= c13_data.size;){
      // read header
      CV_C13_SubSectionHeader *hdr = (CV_C13_SubSectionHeader*)(c13_data.str + cursor);
      
      // get sub section info
      U32 sub_section_off = cursor + sizeof(*hdr);
      U32 sub_section_size_raw = hdr->size;
      U32 after_sub_section_off_unclamped = sub_section_off + sub_section_size_raw;
      U32 after_sub_section_off = ClampTop(after_sub_section_off_unclamped, c13_data.size);
      U32 sub_section_size = after_sub_section_off - sub_section_off;
      
      // emit sub section
      if (!(hdr->kind & CV_C13_SubSectionKind_IgnoreFlag)){
        CV_C13SubSectionNode *node = push_array(arena, CV_C13SubSectionNode, 1);
        SLLQueuePush(first, last, node);
        count += 1;
        node->kind = hdr->kind;
        node->off = sub_section_off;
        node->size = sub_section_size;
        
        if (hdr->kind == CV_C13_SubSectionKind_FileChksms){
          file_chksms = node;
        }
      }
      
      // move cursor
      cursor = AlignPow2(after_sub_section_off, 4);
    }
  }
  
  // parse each section
  for (CV_C13SubSectionNode *node = first;
       node != 0;
       node = node->next){
    U8 *first = c13_data.str + node->off;
    U32 cap = node->size;
    
    switch (node->kind){
      case CV_C13_SubSectionKind_Lines:
      {
        // read header
        if (sizeof(CV_C13_SubSecLinesHeader) <= cap){
          U32 read_off = 0;
          U64 read_off_opl = node->size;
          CV_C13_SubSecLinesHeader *hdr = (CV_C13_SubSecLinesHeader*)(first + read_off);
          read_off += sizeof(*hdr);
          
          // extract top level info
          U32 sec_idx = hdr->sec;
          B32 has_cols = !!(hdr->flags & CV_C13_SubSecLinesFlag_HasColumns);
          U64 secrel_off = hdr->sec_off;
          U64 secrel_opl = secrel_off + hdr->len;
          U64 sec_base_off = sections->sections[sec_idx - 1].voff;
          
          // rjf: bad section index -> skip
          if(sec_idx < 1 || sections->count < sec_idx)
          {
            continue;
          }
          
          // read files
          for(;read_off+sizeof(CV_C13_File) <= read_off_opl;)
          {
            // rjf: grab next file header
            CV_C13_File *file = (CV_C13_File*)(first + read_off);
            U32 file_off = file->file_off;
            U32 line_count_unclamped = file->num_lines;
            U32 block_size = file->block_size;
            
            // file_name from file_off
            String8 file_name = {0};
            if (file_off + sizeof(CV_C13_Checksum) <= file_chksms->size){
              CV_C13_Checksum *checksum = (CV_C13_Checksum*)(c13_data.str + file_chksms->off + file_off);
              U32 name_off = checksum->name_off;
              file_name = pdb_strtbl_string_from_off(strtbl, name_off);
            }
            
            // array layouts
            U32 line_item_size = sizeof(CV_C13_Line);
            if (has_cols){
              line_item_size += sizeof(CV_C13_Column);
            }
            
            U32 line_array_off = read_off + sizeof(*file);
            U32 line_count_max = (read_off_opl - line_array_off) / line_item_size;
            U32 line_count = ClampTop(line_count_unclamped, line_count_max);
            
            U32 col_array_off = line_array_off + line_count*sizeof(CV_C13_Line);
            
            // parse lines
            U64 *voffs = push_array_no_zero(arena, U64, line_count + 1);
            U32 *line_nums = push_array_no_zero(arena, U32, line_count);
            
            {
              CV_C13_Line *line_ptr = (CV_C13_Line*)(first + line_array_off);
              CV_C13_Line *line_opl = line_ptr + line_count;
              
              // TODO(allen): check order correctness here
              
              U32 i = 0;
              for (; line_ptr < line_opl; line_ptr += 1, i += 1){
                voffs[i] = line_ptr->off + secrel_off + sec_base_off;
                line_nums[i] = CV_C13_LineFlags_ExtractLineNumber(line_ptr->flags);
              }
              voffs[i] = secrel_opl + sec_base_off;
            }
            
            // emit parsed lines
            CV_C13LinesParsedNode *lines_parsed_node = push_array(arena, CV_C13LinesParsedNode, 1);
            CV_C13LinesParsed *lines_parsed = &lines_parsed_node->v;
            lines_parsed->sec_idx = sec_idx;
            lines_parsed->file_off = file_off;
            lines_parsed->secrel_base_off = secrel_off;
            lines_parsed->file_name = file_name;
            lines_parsed->voffs  = voffs;
            lines_parsed->line_nums = line_nums;
            lines_parsed->line_count = line_count;
            SLLQueuePush(node->lines_first, node->lines_last, lines_parsed_node);
            
            // rjf: advance
            read_off += sizeof(*file);
            read_off += line_item_size*line_count;
          }
        }
      }break;
    }
  }
  
  // convert to result
  CV_C13Parsed *result = push_array(arena, CV_C13Parsed, 1);
  result->first_sub_section = first;
  result->last_sub_section = last;
  result->sub_section_count = count;
  result->file_chksms_sub_section = file_chksms;
  
  ProfEnd();
  
  return(result);
}
