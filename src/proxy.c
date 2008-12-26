/*
 * IDL Compiler
 *
 * Copyright 2002 Ove Kaaven
 * Copyright 2004 Mike McCormack
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <string.h>
#include <ctype.h>

#include "widl.h"
#include "utils.h"
#include "parser.h"
#include "header.h"
#include "typegen.h"
#include "expr.h"

#define END_OF_LIST(list)       \
  do {                          \
    if (list) {                 \
      while (NEXT_LINK(list))   \
        list = NEXT_LINK(list); \
    }                           \
  } while(0)

static FILE* proxy;
static int indent = 0;

/* FIXME: support generation of stubless proxies */

static void print_proxy( const char *format, ... )
{
  va_list va;
  va_start( va, format );
  print( proxy, indent, format, va );
  va_end( va );
}

static void write_stubdescproto(void)
{
  print_proxy( "static const MIDL_STUB_DESC Object_StubDesc;\n");
  print_proxy( "\n");
}

static void write_stubdesc(int expr_eval_routines)
{
  print_proxy( "static const MIDL_STUB_DESC Object_StubDesc =\n{\n");
  indent++;
  print_proxy( "0,\n");
  print_proxy( "NdrOleAllocate,\n");
  print_proxy( "NdrOleFree,\n");
  print_proxy( "{0}, 0, 0, %s, 0,\n", expr_eval_routines ? "ExprEvalRoutines" : "0");
  print_proxy( "__MIDL_TypeFormatString.Format,\n");
  print_proxy( "1, /* -error bounds_check flag */\n");
  print_proxy( "0x10001, /* Ndr library version */\n");
  print_proxy( "0,\n");
  print_proxy( "0x50100a4, /* MIDL Version 5.1.164 */\n");
  print_proxy( "0,\n");
  print_proxy("%s,\n", list_empty(&user_type_list) ? "0" : "UserMarshalRoutines");
  print_proxy( "0,  /* notify & notify_flag routine table */\n");
  print_proxy( "1,  /* Flags */\n");
  print_proxy( "0,  /* Reserved3 */\n");
  print_proxy( "0,  /* Reserved4 */\n");
  print_proxy( "0   /* Reserved5 */\n");
  indent--;
  print_proxy( "};\n");
  print_proxy( "\n");
}

static void init_proxy(const statement_list_t *stmts)
{
  if (proxy) return;
  if(!(proxy = fopen(proxy_name, "w")))
    error("Could not open %s for output\n", proxy_name);
  print_proxy( "/*** Autogenerated by WIDL %s from %s - Do not edit ***/\n", PACKAGE_VERSION, input_name);
  print_proxy( "\n");
  print_proxy( "#ifndef __REDQ_RPCPROXY_H_VERSION__\n");
  print_proxy( "#define __REQUIRED_RPCPROXY_H_VERSION__ 440\n");
  print_proxy( "#endif /* __REDQ_RPCPROXY_H_VERSION__ */\n");
  print_proxy( "\n");
  print_proxy( "#define __midl_proxy\n");
  print_proxy( "#include \"objbase.h\"\n");
  print_proxy( "#include \"rpcproxy.h\"\n");
  print_proxy( "#ifndef __RPCPROXY_H_VERSION__\n");
  print_proxy( "#error This code needs a newer version of rpcproxy.h\n");
  print_proxy( "#endif /* __RPCPROXY_H_VERSION__ */\n");
  print_proxy( "\n");
  print_proxy( "#include \"%s\"\n", header_name);
  print_proxy( "\n");
  print_proxy( "#ifndef DECLSPEC_HIDDEN\n");
  print_proxy( "#define DECLSPEC_HIDDEN\n");
  print_proxy( "#endif\n");
  print_proxy( "\n");
  write_exceptions( proxy );
  print_proxy( "\n");
  print_proxy( "struct __proxy_frame\n");
  print_proxy( "{\n");
  print_proxy( "    __DECL_EXCEPTION_FRAME\n");
  print_proxy( "    MIDL_STUB_MESSAGE _StubMsg;\n");
  print_proxy( "    void             *This;\n");
  print_proxy( "};\n");
  print_proxy( "\n");
  print_proxy("static int __proxy_filter( struct __proxy_frame *__frame )\n");
  print_proxy( "{\n");
  print_proxy( "    return (__frame->_StubMsg.dwStubPhase != PROXY_SENDRECEIVE);\n");
  print_proxy( "}\n");
  print_proxy( "\n");
}

static void clear_output_vars( const var_list_t *args )
{
  const var_t *arg;

  if (!args) return;
  LIST_FOR_EACH_ENTRY( arg, args, const var_t, entry )
  {
    if (is_attr(arg->attrs, ATTR_OUT) && !is_attr(arg->attrs, ATTR_IN)) {
      print_proxy( "if(%s)\n", arg->name );
      indent++;
      print_proxy( "MIDL_memset( %s, 0, sizeof( *%s ));\n", arg->name, arg->name );
      indent--;
    }
  }
}

int is_var_ptr(const var_t *v)
{
  return is_ptr(v->type);
}

int cant_be_null(const var_t *v)
{
  /* Search backwards for the most recent pointer attribute.  */
  const attr_list_t *attrs = v->attrs;
  const type_t *type = v->type;

  /* context handles have their own checking so they can be null for the
   * purposes of null ref pointer checking */
  if (is_aliaschain_attr(type, ATTR_CONTEXTHANDLE))
      return 0;

  if (! attrs && type)
  {
    attrs = type->attrs;
    type = type->ref;
  }

  while (attrs)
  {
    int t = get_attrv(attrs, ATTR_POINTERTYPE);

    if (t == RPC_FC_FP || t == RPC_FC_OP || t == RPC_FC_UP)
      return 0;

    if (t == RPC_FC_RP)
      return 1;

    if (type)
    {
      attrs = type->attrs;
      type = type->ref;
    }
    else
      attrs = NULL;
  }

  return 1;                             /* Default is RPC_FC_RP.  */
}

static int need_delegation(const type_t *iface)
{
    return iface->ref && iface->ref->ref && iface->ref->ignore;
}

static void proxy_check_pointers( const var_list_t *args )
{
  const var_t *arg;

  if (!args) return;
  LIST_FOR_EACH_ENTRY( arg, args, const var_t, entry )
  {
    if (is_var_ptr(arg) && cant_be_null(arg)) {
        print_proxy( "if(!%s)\n", arg->name );
        indent++;
        print_proxy( "RpcRaiseException(RPC_X_NULL_REF_POINTER);\n");
        indent--;
    }
  }
}

static void free_variable( const var_t *arg, const char *local_var_prefix )
{
  unsigned int type_offset = arg->type->typestring_offset;
  expr_t *iid;
  type_t *type = arg->type;
  expr_t *size = get_size_is_expr(type, arg->name);

  if (size)
  {
    print_proxy( "__frame->_StubMsg.MaxCount = " );
    write_expr(proxy, size, 0, 1, NULL, NULL, local_var_prefix);
    fprintf(proxy, ";\n\n");
    print_proxy( "NdrClearOutParameters( &__frame->_StubMsg, ");
    fprintf(proxy, "&__MIDL_TypeFormatString.Format[%u], ", type_offset );
    fprintf(proxy, "(void*)%s );\n", arg->name );
    return;
  }

  switch( type->type )
  {
  case RPC_FC_BYTE:
  case RPC_FC_CHAR:
  case RPC_FC_WCHAR:
  case RPC_FC_SHORT:
  case RPC_FC_USHORT:
  case RPC_FC_ENUM16:
  case RPC_FC_LONG:
  case RPC_FC_ULONG:
  case RPC_FC_ENUM32:
  case RPC_FC_STRUCT:
    break;

  case RPC_FC_FP:
  case RPC_FC_IP:
    iid = get_attrp( arg->attrs, ATTR_IIDIS );
    if( iid )
    {
      print_proxy( "__frame->_StubMsg.MaxCount = (ULONG_PTR) " );
      write_expr(proxy, iid, 1, 1, NULL, NULL, local_var_prefix);
      print_proxy( ";\n\n" );
    }
    print_proxy( "NdrClearOutParameters( &__frame->_StubMsg, ");
    fprintf(proxy, "&__MIDL_TypeFormatString.Format[%u], ", type_offset );
    fprintf(proxy, "(void*)%s );\n", arg->name );
    break;

  default:
    print_proxy("/* FIXME: %s code for %s type %d missing */\n", __FUNCTION__, arg->name, type->type );
  }
}

static void proxy_free_variables( var_list_t *args, const char *local_var_prefix )
{
  const var_t *arg;

  if (!args) return;
  LIST_FOR_EACH_ENTRY( arg, args, const var_t, entry )
    if (is_attr(arg->attrs, ATTR_OUT))
    {
      free_variable( arg, local_var_prefix );
      fprintf(proxy, "\n");
    }
}

static void gen_proxy(type_t *iface, const func_t *cur, int idx,
                      unsigned int proc_offset)
{
  var_t *def = cur->def;
  int has_ret = !is_void(get_func_return_type(cur));
  int has_full_pointer = is_full_pointer_function(cur);
  const char *callconv = get_attrp(def->type->attrs, ATTR_CALLCONV);
  if (!callconv) callconv = "";

  indent = 0;
  print_proxy( "static void __finally_%s_%s_Proxy( struct __proxy_frame *__frame )\n",
               iface->name, get_name(def) );
  print_proxy( "{\n");
  indent++;
  if (has_full_pointer) write_full_pointer_free(proxy, indent, cur);
  print_proxy( "NdrProxyFreeBuffer( __frame->This, &__frame->_StubMsg );\n" );
  indent--;
  print_proxy( "}\n");
  print_proxy( "\n");

  write_type_decl_left(proxy, get_func_return_type(cur));
  print_proxy( " %s %s_%s_Proxy(\n", callconv, iface->name, get_name(def));
  write_args(proxy, cur->args, iface->name, 1, TRUE);
  print_proxy( ")\n");
  print_proxy( "{\n");
  indent ++;
  print_proxy( "struct __proxy_frame __f, * const __frame = &__f;\n" );
  /* local variables */
  if (has_ret) {
    print_proxy( "" );
    write_type_decl_left(proxy, get_func_return_type(cur));
    print_proxy( " _RetVal;\n");
  }
  print_proxy( "RPC_MESSAGE _RpcMessage;\n" );
  if (has_ret) {
    if (decl_indirect(get_func_return_type(cur)))
      print_proxy("void *_p_%s = &%s;\n",
                 "_RetVal", "_RetVal");
  }
  print_proxy( "\n");

  print_proxy( "RpcExceptionInit( __proxy_filter, __finally_%s_%s_Proxy );\n", iface->name, get_name(def) );
  print_proxy( "__frame->This = This;\n" );

  if (has_full_pointer)
    write_full_pointer_init(proxy, indent, cur, FALSE);

  /* FIXME: trace */
  clear_output_vars( cur->args );

  print_proxy( "RpcTryExcept\n" );
  print_proxy( "{\n" );
  indent++;
  print_proxy( "NdrProxyInitialize(This, &_RpcMessage, &__frame->_StubMsg, &Object_StubDesc, %d);\n", idx);
  proxy_check_pointers( cur->args );

  print_proxy( "RpcTryFinally\n" );
  print_proxy( "{\n" );
  indent++;

  write_remoting_arguments(proxy, indent, cur, "", PASS_IN, PHASE_BUFFERSIZE);

  print_proxy( "NdrProxyGetBuffer(This, &__frame->_StubMsg);\n" );

  write_remoting_arguments(proxy, indent, cur, "", PASS_IN, PHASE_MARSHAL);

  print_proxy( "NdrProxySendReceive(This, &__frame->_StubMsg);\n" );
  fprintf(proxy, "\n");
  print_proxy( "__frame->_StubMsg.BufferStart = _RpcMessage.Buffer;\n" );
  print_proxy( "__frame->_StubMsg.BufferEnd   = __frame->_StubMsg.BufferStart + _RpcMessage.BufferLength;\n\n" );

  print_proxy("if ((_RpcMessage.DataRepresentation & 0xffff) != NDR_LOCAL_DATA_REPRESENTATION)\n");
  indent++;
  print_proxy("NdrConvert( &__frame->_StubMsg, &__MIDL_ProcFormatString.Format[%u]);\n", proc_offset );
  indent--;
  fprintf(proxy, "\n");

  write_remoting_arguments(proxy, indent, cur, "", PASS_OUT, PHASE_UNMARSHAL);

  if (has_ret)
  {
      if (decl_indirect(get_func_return_type(cur)))
          print_proxy("MIDL_memset(&%s, 0, sizeof(%s));\n", "_RetVal", "_RetVal");
      else if (is_ptr(get_func_return_type(cur)) || is_array(get_func_return_type(cur)))
          print_proxy("%s = 0;\n", "_RetVal");
      write_remoting_arguments(proxy, indent, cur, "", PASS_RETURN, PHASE_UNMARSHAL);
  }

  indent--;
  print_proxy( "}\n");
  print_proxy( "RpcFinally\n" );
  print_proxy( "{\n" );
  indent++;
  print_proxy( "__finally_%s_%s_Proxy( __frame );\n", iface->name, get_name(def) );
  indent--;
  print_proxy( "}\n");
  print_proxy( "RpcEndFinally\n" );
  indent--;
  print_proxy( "}\n" );
  print_proxy( "RpcExcept(__frame->_StubMsg.dwStubPhase != PROXY_SENDRECEIVE)\n" );
  print_proxy( "{\n" );
  if (has_ret) {
    indent++;
    proxy_free_variables( cur->args, "" );
    print_proxy( "_RetVal = NdrProxyErrorHandler(RpcExceptionCode());\n" );
    indent--;
  }
  print_proxy( "}\n" );
  print_proxy( "RpcEndExcept\n" );

  if (has_ret) {
    print_proxy( "return _RetVal;\n" );
  }
  indent--;
  print_proxy( "}\n");
  print_proxy( "\n");
}

static void gen_stub(type_t *iface, const func_t *cur, const char *cas,
                     unsigned int proc_offset)
{
  var_t *def = cur->def;
  const var_t *arg;
  int has_ret = !is_void(get_func_return_type(cur));
  int has_full_pointer = is_full_pointer_function(cur);

  indent = 0;
  print_proxy( "struct __frame_%s_%s_Stub\n{\n", iface->name, get_name(def));
  indent++;
  print_proxy( "__DECL_EXCEPTION_FRAME\n" );
  print_proxy( "MIDL_STUB_MESSAGE _StubMsg;\n");
  print_proxy( "%s * _This;\n", iface->name );
  declare_stub_args( proxy, indent, cur );
  indent--;
  print_proxy( "};\n\n" );

  print_proxy( "static void __finally_%s_%s_Stub(", iface->name, get_name(def) );
  print_proxy( " struct __frame_%s_%s_Stub *__frame )\n{\n", iface->name, get_name(def) );
  indent++;
  write_remoting_arguments(proxy, indent, cur, "__frame->", PASS_OUT, PHASE_FREE);
  if (has_full_pointer)
    write_full_pointer_free(proxy, indent, cur);
  indent--;
  print_proxy( "}\n\n" );

  print_proxy( "void __RPC_STUB %s_%s_Stub(\n", iface->name, get_name(def));
  indent++;
  print_proxy( "IRpcStubBuffer* This,\n");
  print_proxy( "IRpcChannelBuffer *_pRpcChannelBuffer,\n");
  print_proxy( "PRPC_MESSAGE _pRpcMessage,\n");
  print_proxy( "DWORD* _pdwStubPhase)\n");
  indent--;
  print_proxy( "{\n");
  indent++;
  print_proxy( "struct __frame_%s_%s_Stub __f, * const __frame = &__f;\n\n",
               iface->name, get_name(def) );

  print_proxy("__frame->_This = (%s*)((CStdStubBuffer*)This)->pvServerObject;\n\n", iface->name);

  /* FIXME: trace */

  print_proxy("NdrStubInitialize(_pRpcMessage, &__frame->_StubMsg, &Object_StubDesc, _pRpcChannelBuffer);\n");
  fprintf(proxy, "\n");
  print_proxy( "RpcExceptionInit( 0, __finally_%s_%s_Stub );\n", iface->name, get_name(def) );

  write_parameters_init(proxy, indent, cur, "__frame->");

  print_proxy("RpcTryFinally\n");
  print_proxy("{\n");
  indent++;
  if (has_full_pointer)
    write_full_pointer_init(proxy, indent, cur, TRUE);
  print_proxy("if ((_pRpcMessage->DataRepresentation & 0xffff) != NDR_LOCAL_DATA_REPRESENTATION)\n");
  indent++;
  print_proxy("NdrConvert( &__frame->_StubMsg, &__MIDL_ProcFormatString.Format[%u]);\n", proc_offset );
  indent--;
  fprintf(proxy, "\n");

  write_remoting_arguments(proxy, indent, cur, "__frame->", PASS_IN, PHASE_UNMARSHAL);
  fprintf(proxy, "\n");

  assign_stub_out_args( proxy, indent, cur, "__frame->" );

  print_proxy("*_pdwStubPhase = STUB_CALL_SERVER;\n");
  fprintf(proxy, "\n");
  print_proxy("");
  if (has_ret) fprintf(proxy, "__frame->_RetVal = ");
  if (cas) fprintf(proxy, "%s_%s_Stub", iface->name, cas);
  else fprintf(proxy, "__frame->_This->lpVtbl->%s", get_name(def));
  fprintf(proxy, "(__frame->_This");

  if (cur->args)
  {
      LIST_FOR_EACH_ENTRY( arg, cur->args, const var_t, entry )
          fprintf(proxy, ", %s__frame->%s", arg->type->declarray ? "*" : "", arg->name);
  }
  fprintf(proxy, ");\n");
  fprintf(proxy, "\n");
  print_proxy("*_pdwStubPhase = STUB_MARSHAL;\n");
  fprintf(proxy, "\n");

  write_remoting_arguments(proxy, indent, cur, "__frame->", PASS_OUT, PHASE_BUFFERSIZE);

  if (!is_void(get_func_return_type(cur)))
    write_remoting_arguments(proxy, indent, cur, "__frame->", PASS_RETURN, PHASE_BUFFERSIZE);

  print_proxy("NdrStubGetBuffer(This, _pRpcChannelBuffer, &__frame->_StubMsg);\n");

  write_remoting_arguments(proxy, indent, cur, "__frame->", PASS_OUT, PHASE_MARSHAL);
  fprintf(proxy, "\n");

  /* marshall the return value */
  if (!is_void(get_func_return_type(cur)))
    write_remoting_arguments(proxy, indent, cur, "__frame->", PASS_RETURN, PHASE_MARSHAL);

  indent--;
  print_proxy("}\n");
  print_proxy("RpcFinally\n");
  print_proxy("{\n");
  indent++;
  print_proxy( "__finally_%s_%s_Stub( __frame );\n", iface->name, get_name(def) );
  indent--;
  print_proxy("}\n");
  print_proxy("RpcEndFinally\n");

  print_proxy("_pRpcMessage->BufferLength = __frame->_StubMsg.Buffer - (unsigned char *)_pRpcMessage->Buffer;\n");
  indent--;

  print_proxy("}\n");
  print_proxy("\n");
}

static int count_methods(type_t *iface)
{
    const func_t *cur;
    int count = 0;

    if (iface->ref) count = count_methods(iface->ref);
    if (iface->funcs)
        LIST_FOR_EACH_ENTRY( cur, iface->funcs, const func_t, entry )
            if (!is_callas(cur->def->attrs)) count++;
    return count;
}

static int write_proxy_methods(type_t *iface, int skip)
{
  const func_t *cur;
  int i = 0;

  if (iface->ref) i = write_proxy_methods(iface->ref, need_delegation(iface));
  if (iface->funcs) LIST_FOR_EACH_ENTRY( cur, iface->funcs, const func_t, entry ) {
    var_t *def = cur->def;
    if (!is_callas(def->attrs)) {
      if (i) fprintf(proxy, ",\n");
      if (skip) print_proxy( "0  /* %s_%s_Proxy */", iface->name, get_name(def));
      else print_proxy( "%s_%s_Proxy", iface->name, get_name(def));
      i++;
    }
  }
  return i;
}

static int write_stub_methods(type_t *iface, int skip)
{
  const func_t *cur;
  int i = 0;

  if (iface->ref) i = write_stub_methods(iface->ref, need_delegation(iface));
  else return i; /* skip IUnknown */

  if (iface->funcs) LIST_FOR_EACH_ENTRY( cur, iface->funcs, const func_t, entry ) {
    var_t *def = cur->def;
    if (!is_local(def->attrs)) {
      if (i) fprintf(proxy,",\n");
      if (skip) print_proxy("STUB_FORWARDING_FUNCTION");
      else print_proxy( "%s_%s_Stub", iface->name, get_name(def));
      i++;
    }
  }
  return i;
}

static void write_proxy(type_t *iface, unsigned int *proc_offset)
{
  int midx = -1, count;
  const func_t *cur;

  /* FIXME: check for [oleautomation], shouldn't generate proxies/stubs if specified */

  fprintf(proxy, "/*****************************************************************************\n");
  fprintf(proxy, " * %s interface\n", iface->name);
  fprintf(proxy, " */\n");
  if (iface->funcs) LIST_FOR_EACH_ENTRY( cur, iface->funcs, const func_t, entry )
  {
    const var_t *def = cur->def;
    if (!is_local(def->attrs)) {
      const var_t *cas = is_callas(def->attrs);
      const char *cname = cas ? cas->name : NULL;
      int idx = cur->idx;
      if (cname) {
          const func_t *m;
          LIST_FOR_EACH_ENTRY( m, iface->funcs, const func_t, entry )
              if (!strcmp(m->def->name, cname))
              {
                  idx = m->idx;
                  break;
              }
      }
      gen_proxy(iface, cur, idx, *proc_offset);
      gen_stub(iface, cur, cname, *proc_offset);
      *proc_offset += get_size_procformatstring_func( cur );
      if (midx == -1) midx = idx;
      else if (midx != idx) error("method index mismatch in write_proxy\n");
      midx++;
    }
  }

  count = count_methods(iface);
  if (midx != -1 && midx != count) error("invalid count %u/%u\n", count, midx);

  /* proxy vtable */
  print_proxy( "static const CINTERFACE_PROXY_VTABLE(%d) _%sProxyVtbl =\n", count, iface->name);
  print_proxy( "{\n");
  indent++;
  print_proxy( "{\n", iface->name);
  indent++;
  print_proxy( "&IID_%s,\n", iface->name);
  indent--;
  print_proxy( "},\n");
  print_proxy( "{\n");
  indent++;
  write_proxy_methods(iface, FALSE);
  fprintf(proxy, "\n");
  indent--;
  print_proxy( "}\n");
  indent--;
  print_proxy( "};\n");
  fprintf(proxy, "\n\n");

  /* stub vtable */
  print_proxy( "static const PRPC_STUB_FUNCTION %s_table[] =\n", iface->name);
  print_proxy( "{\n");
  indent++;
  write_stub_methods(iface, FALSE);
  fprintf(proxy, "\n");
  indent--;
  fprintf(proxy, "};\n");
  print_proxy( "\n");
  print_proxy( "static %sCInterfaceStubVtbl _%sStubVtbl =\n",
               need_delegation(iface) ? "" : "const ", iface->name);
  print_proxy( "{\n");
  indent++;
  print_proxy( "{\n");
  indent++;
  print_proxy( "&IID_%s,\n", iface->name);
  print_proxy( "0,\n");
  print_proxy( "%d,\n", count);
  print_proxy( "&%s_table[-3],\n", iface->name);
  indent--;
  print_proxy( "},\n", iface->name);
  print_proxy( "{\n");
  indent++;
  print_proxy( "CStdStubBuffer_%s\n", need_delegation(iface) ? "DELEGATING_METHODS" : "METHODS");
  indent--;
  print_proxy( "}\n");
  indent--;
  print_proxy( "};\n");
  print_proxy( "\n");
}

static int does_any_iface(const statement_list_t *stmts, type_pred_t pred)
{
  const statement_t *stmt;

  if (stmts)
    LIST_FOR_EACH_ENTRY(stmt, stmts, const statement_t, entry)
    {
      if (stmt->type == STMT_LIBRARY)
      {
          if (does_any_iface(stmt->u.lib->stmts, pred))
              return TRUE;
      }
      else if (stmt->type == STMT_TYPE && stmt->u.type->type == RPC_FC_IP)
      {
        if (pred(stmt->u.type))
          return TRUE;
      }
    }

  return FALSE;
}

int need_proxy(const type_t *iface)
{
  return is_object(iface->attrs) && !is_local(iface->attrs);
}

int need_stub(const type_t *iface)
{
  return !is_object(iface->attrs) && !is_local(iface->attrs);
}

int need_proxy_file(const statement_list_t *stmts)
{
  return does_any_iface(stmts, need_proxy);
}

int need_stub_files(const statement_list_t *stmts)
{
  return does_any_iface(stmts, need_stub);
}

static void write_proxy_stmts(const statement_list_t *stmts, unsigned int *proc_offset)
{
  const statement_t *stmt;
  if (stmts) LIST_FOR_EACH_ENTRY( stmt, stmts, const statement_t, entry )
  {
    if (stmt->type == STMT_LIBRARY)
      write_proxy_stmts(stmt->u.lib->stmts, proc_offset);
    else if (stmt->type == STMT_TYPE && stmt->u.type->type == RPC_FC_IP)
    {
      if (need_proxy(stmt->u.type))
        write_proxy(stmt->u.type, proc_offset);
    }
  }
}

static int cmp_iid( const void *ptr1, const void *ptr2 )
{
    const type_t * const *iface1 = ptr1;
    const type_t * const *iface2 = ptr2;
    const UUID *uuid1 = get_attrp( (*iface1)->attrs, ATTR_UUID );
    const UUID *uuid2 = get_attrp( (*iface2)->attrs, ATTR_UUID );
    return memcmp( uuid1, uuid2, sizeof(UUID) );
}

static void build_iface_list( const statement_list_t *stmts, type_t **ifaces[], int *count )
{
    const statement_t *stmt;

    if (!stmts) return;
    LIST_FOR_EACH_ENTRY( stmt, stmts, const statement_t, entry )
    {
        if (stmt->type == STMT_LIBRARY)
            build_iface_list(stmt->u.lib->stmts, ifaces, count);
        else if (stmt->type == STMT_TYPE && stmt->u.type->type == RPC_FC_IP)
        {
            type_t *iface = stmt->u.type;
            if (iface->ref && need_proxy(iface))
            {
                *ifaces = xrealloc( *ifaces, (*count + 1) * sizeof(*ifaces) );
                (*ifaces)[(*count)++] = iface;
            }
        }
    }
}

static type_t **sort_interfaces( const statement_list_t *stmts, int *count )
{
    type_t **ifaces = NULL;

    *count = 0;
    build_iface_list( stmts, &ifaces, count );
    qsort( ifaces, *count, sizeof(*ifaces), cmp_iid );
    return ifaces;
}

static void write_proxy_routines(const statement_list_t *stmts)
{
  int expr_eval_routines;
  unsigned int proc_offset = 0;

  write_formatstringsdecl(proxy, indent, stmts, need_proxy);
  write_stubdescproto();
  write_proxy_stmts(stmts, &proc_offset);

  expr_eval_routines = write_expr_eval_routines(proxy, proxy_token);
  if (expr_eval_routines)
      write_expr_eval_routine_list(proxy, proxy_token);
  write_user_quad_list(proxy);
  write_stubdesc(expr_eval_routines);

  print_proxy( "#if !defined(__RPC_WIN%u__)\n", pointer_size == 8 ? 64 : 32);
  print_proxy( "#error Currently only Wine and WIN32 are supported.\n");
  print_proxy( "#endif\n");
  print_proxy( "\n");
  write_procformatstring(proxy, stmts, need_proxy);
  write_typeformatstring(proxy, stmts, need_proxy);

}

void write_proxies(const statement_list_t *stmts)
{
  char *file_id = proxy_token;
  int i, count, have_baseiid;
  type_t **interfaces;

  if (!do_proxies) return;
  if (do_everything && !need_proxy_file(stmts)) return;

  init_proxy(stmts);
  if(!proxy) return;

  if (do_win32 && do_win64)
  {
      fprintf(proxy, "\n#ifndef _WIN64\n\n");
      pointer_size = 4;
      write_proxy_routines( stmts );
      fprintf(proxy, "\n#else /* _WIN64 */\n\n");
      pointer_size = 8;
      write_proxy_routines( stmts );
      fprintf(proxy, "#endif /* _WIN64 */\n\n");
  }
  else if (do_win32)
  {
      pointer_size = 4;
      write_proxy_routines( stmts );
  }
  else if (do_win64)
  {
      pointer_size = 8;
      write_proxy_routines( stmts );
  }

  interfaces = sort_interfaces(stmts, &count);
  fprintf(proxy, "static const CInterfaceProxyVtbl* const _%s_ProxyVtblList[] =\n", file_id);
  fprintf(proxy, "{\n");
  for (i = 0; i < count; i++)
      fprintf(proxy, "    (const CInterfaceProxyVtbl*)&_%sProxyVtbl,\n", interfaces[i]->name);
  fprintf(proxy, "    0\n");
  fprintf(proxy, "};\n");
  fprintf(proxy, "\n");

  fprintf(proxy, "static const CInterfaceStubVtbl* const _%s_StubVtblList[] =\n", file_id);
  fprintf(proxy, "{\n");
  for (i = 0; i < count; i++)
      fprintf(proxy, "    &_%sStubVtbl,\n", interfaces[i]->name);
  fprintf(proxy, "    0\n");
  fprintf(proxy, "};\n");
  fprintf(proxy, "\n");

  fprintf(proxy, "static PCInterfaceName const _%s_InterfaceNamesList[] =\n", file_id);
  fprintf(proxy, "{\n");
  for (i = 0; i < count; i++)
      fprintf(proxy, "    \"%s\",\n", interfaces[i]->name);
  fprintf(proxy, "    0\n");
  fprintf(proxy, "};\n");
  fprintf(proxy, "\n");

  if ((have_baseiid = does_any_iface(stmts, need_delegation)))
  {
      fprintf(proxy, "static const IID * _%s_BaseIIDList[] =\n", file_id);
      fprintf(proxy, "{\n");
      for (i = 0; i < count; i++)
      {
          if (need_delegation(interfaces[i]))
              fprintf( proxy, "    &IID_%s,  /* %s */\n", interfaces[i]->ref->name, interfaces[i]->name );
          else
              fprintf( proxy, "    0,\n" );
      }
      fprintf(proxy, "    0\n");
      fprintf(proxy, "};\n");
      fprintf(proxy, "\n");
  }

  fprintf(proxy, "static int __stdcall _%s_IID_Lookup(const IID* pIID, int* pIndex)\n", file_id);
  fprintf(proxy, "{\n");
  fprintf(proxy, "    int low = 0, high = %d;\n", count - 1);
  fprintf(proxy, "\n");
  fprintf(proxy, "    while (low <= high)\n");
  fprintf(proxy, "    {\n");
  fprintf(proxy, "        int pos = (low + high) / 2;\n");
  fprintf(proxy, "        int res = IID_GENERIC_CHECK_IID(_%s, pIID, pos);\n", file_id);
  fprintf(proxy, "        if (!res) { *pIndex = pos; return 1; }\n");
  fprintf(proxy, "        if (res > 0) low = pos + 1;\n");
  fprintf(proxy, "        else high = pos - 1;\n");
  fprintf(proxy, "    }\n");
  fprintf(proxy, "    return 0;\n");
  fprintf(proxy, "}\n");
  fprintf(proxy, "\n");

  fprintf(proxy, "const ExtendedProxyFileInfo %s_ProxyFileInfo DECLSPEC_HIDDEN =\n", file_id);
  fprintf(proxy, "{\n");
  fprintf(proxy, "    (const PCInterfaceProxyVtblList*)_%s_ProxyVtblList,\n", file_id);
  fprintf(proxy, "    (const PCInterfaceStubVtblList*)_%s_StubVtblList,\n", file_id);
  fprintf(proxy, "    _%s_InterfaceNamesList,\n", file_id);
  if (have_baseiid) fprintf(proxy, "    _%s_BaseIIDList,\n", file_id);
  else fprintf(proxy, "    0,\n");
  fprintf(proxy, "    _%s_IID_Lookup,\n", file_id);
  fprintf(proxy, "    %d,\n", count);
  fprintf(proxy, "    1,\n");
  fprintf(proxy, "    0,\n");
  fprintf(proxy, "    0,\n");
  fprintf(proxy, "    0,\n");
  fprintf(proxy, "    0\n");
  fprintf(proxy, "};\n");

  fclose(proxy);
}
