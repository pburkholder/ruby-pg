/*
 * pg_coder.c - PG::Coder class extension
 *
 */

#include "pg.h"

VALUE rb_cPG_Coder;
VALUE rb_cPG_SimpleCoder;
VALUE rb_cPG_SimpleEncoder;
VALUE rb_cPG_SimpleDecoder;
VALUE rb_cPG_CompositeCoder;
VALUE rb_cPG_CompositeEncoder;
VALUE rb_cPG_CompositeDecoder;
static ID s_id_encode;
static ID s_id_decode;
static ID s_id_CFUNC;

/*
 * Document-class: PG::Coder
 *
 * This is the base class for all type cast encoder and decoder classes.
 *
 */

static VALUE
pg_coder_allocate( VALUE klass )
{
	rb_raise( rb_eTypeError, "PG::Coder cannot be instantiated directly");
}

/*
 * Document-class: PG::CompositeCoder
 *
 * This is the base class for all type cast classes of PostgreSQL types,
 * that are made up of some sub type.
 *
 */

void
pg_coder_init_encoder( VALUE self )
{
	t_pg_coder *sval = DATA_PTR( self );
	VALUE klass = rb_class_of(self);
	if( rb_const_defined( klass, s_id_CFUNC ) ){
		VALUE cfunc = rb_const_get( klass, s_id_CFUNC );
		sval->enc_func = DATA_PTR(cfunc);
	} else {
		sval->enc_func = NULL;
	}
	sval->dec_func = NULL;
	sval->coder_obj = self;
	sval->oid = 0;
	sval->format = 0;
	rb_iv_set( self, "@name", Qnil );
}

void
pg_coder_init_decoder( VALUE self )
{
	t_pg_coder *sval = DATA_PTR( self );
	VALUE klass = rb_class_of(self);
	sval->enc_func = NULL;
	if( rb_const_defined( klass, s_id_CFUNC ) ){
		VALUE cfunc = rb_const_get( klass, s_id_CFUNC );
		sval->dec_func = DATA_PTR(cfunc);
	} else {
		sval->dec_func = NULL;
	}
	sval->coder_obj = self;
	sval->oid = 0;
	sval->format = 0;
	rb_iv_set( self, "@name", Qnil );
}

static VALUE
pg_simple_encoder_allocate( VALUE klass )
{
	t_pg_coder *sval;
	VALUE self = Data_Make_Struct( klass, t_pg_coder, NULL, -1, sval );
	pg_coder_init_encoder( self );
	return self;
}

static VALUE
pg_composite_encoder_allocate( VALUE klass )
{
	t_pg_composite_coder *sval;
	VALUE self = Data_Make_Struct( klass, t_pg_composite_coder, NULL, -1, sval );
	pg_coder_init_encoder( self );
	sval->elem = NULL;
	sval->needs_quotation = 1;
	sval->delimiter = ',';
	rb_iv_set( self, "@elements_type", Qnil );
	return self;
}

static VALUE
pg_simple_decoder_allocate( VALUE klass )
{
	t_pg_coder *sval;
	VALUE self = Data_Make_Struct( klass, t_pg_coder, NULL, -1, sval );
	pg_coder_init_decoder( self );
	return self;
}

static VALUE
pg_composite_decoder_allocate( VALUE klass )
{
	t_pg_composite_coder *sval;
	VALUE self = Data_Make_Struct( klass, t_pg_composite_coder, NULL, -1, sval );
	pg_coder_init_decoder( self );
	sval->elem = NULL;
	sval->needs_quotation = 1;
	sval->delimiter = ',';
	rb_iv_set( self, "@elements_type", Qnil );
	return self;
}

static VALUE
pg_coder_encode(VALUE self, VALUE value)
{
	VALUE res;
	VALUE intermediate;
	int len, len2;
	t_pg_coder *type_data = DATA_PTR(self);

	if( !type_data->enc_func ){
		rb_raise(rb_eRuntimeError, "no encoder function defined");
	}

	len = type_data->enc_func( type_data, value, NULL, &intermediate );

	if( len == -1 ){
		/* The intermediate value is a String that can be used directly. */
		return intermediate;
	}

	res = rb_str_new(NULL, 0);
	rb_str_resize( res, len );
	len2 = type_data->enc_func( type_data, value, RSTRING_PTR(res), &intermediate);
	if( len < len2 ){
		rb_bug("%s: result length of first encoder run (%i) is less than second run (%i)",
			rb_obj_classname( self ), len, len2 );
	}
	rb_str_set_len( res, len2 );

	RB_GC_GUARD(intermediate);

	return res;
}

static VALUE
pg_coder_decode(int argc, VALUE *argv, VALUE self)
{
	char *val;
	VALUE tuple = -1;
	VALUE field = -1;
	t_pg_coder *type_data = DATA_PTR(self);

	if(argc < 1 || argc > 3){
		rb_raise(rb_eArgError, "wrong number of arguments (%i for 1..3)", argc);
	}else if(argc >= 3){
		tuple = NUM2INT(argv[1]);
		field = NUM2INT(argv[2]);
	}

	val = StringValuePtr(argv[0]);
	if( !type_data->dec_func ){
		rb_raise(rb_eRuntimeError, "no decoder function defined");
	}

	return type_data->dec_func(type_data, val, RSTRING_LEN(argv[0]), tuple, field, ENCODING_GET(argv[0]));
}

static VALUE
pg_coder_oid_set(VALUE self, VALUE oid)
{
	t_pg_coder *type_data = DATA_PTR(self);
	type_data->oid = NUM2UINT(oid);
	return oid;
}

static VALUE
pg_coder_oid_get(VALUE self)
{
	t_pg_coder *type_data = DATA_PTR(self);
	return UINT2NUM(type_data->oid);
}

static VALUE
pg_coder_format_set(VALUE self, VALUE format)
{
	t_pg_coder *type_data = DATA_PTR(self);
	type_data->format = NUM2INT(format);
	return format;
}

static VALUE
pg_coder_format_get(VALUE self)
{
	t_pg_coder *type_data = DATA_PTR(self);
	return INT2NUM(type_data->format);
}

static VALUE
pg_coder_needs_quotation_set(VALUE self, VALUE needs_quotation)
{
	t_pg_composite_coder *type_data = DATA_PTR(self);
	type_data->needs_quotation = RTEST(needs_quotation);
	return needs_quotation;
}

static VALUE
pg_coder_needs_quotation_get(VALUE self)
{
	t_pg_composite_coder *type_data = DATA_PTR(self);
	return type_data->needs_quotation ? Qtrue : Qfalse;
}

static VALUE
pg_coder_delimiter_set(VALUE self, VALUE delimiter)
{
	t_pg_composite_coder *type_data = DATA_PTR(self);
	StringValue(delimiter);
	if(RSTRING_LEN(delimiter) != 1)
		rb_raise( rb_eArgError, "delimiter size must be one byte");
	type_data->delimiter = *RSTRING_PTR(delimiter);
	return delimiter;
}

static VALUE
pg_coder_delimiter_get(VALUE self)
{
	t_pg_composite_coder *type_data = DATA_PTR(self);
	return rb_str_new(&type_data->delimiter, 1);
}

/*
 *
 */
static VALUE
pg_coder_elements_type_set(VALUE self, VALUE elem_type)
{
	t_pg_composite_coder *p_type = DATA_PTR( self );

	if ( NIL_P(elem_type) ){
		p_type->elem = NULL;
	} else if ( rb_obj_is_kind_of(elem_type, rb_cPG_Coder) ){
		p_type->elem = DATA_PTR( elem_type );
	} else {
		rb_raise( rb_eTypeError, "wrong elements type %s (expected some kind of PG::Coder)",
				rb_obj_classname( elem_type ) );
	}

	rb_iv_set( self, "@elements_type", elem_type );
	return elem_type;
}

void
pg_define_coder( const char *name, void *func, VALUE base_klass, VALUE nsp )
{
	VALUE cfunc_obj = Data_Wrap_Struct( rb_cObject, NULL, NULL, func );
	VALUE coder_klass = rb_define_class_under( nsp, name, base_klass );
	rb_define_const( coder_klass, "CFUNC", cfunc_obj );

	RB_GC_GUARD(cfunc_obj);
}

t_pg_coder_enc_func
pg_coder_enc_func(t_pg_coder *this)
{
	if( this ){
		if( this->enc_func ){
			return this->enc_func;
		}else{
			return pg_text_enc_in_ruby;
		}
	}else{
		/* no element encoder defined -> use std to_str conversion */
		return pg_coder_enc_to_str;
	}
}

void
init_pg_coder()
{
	s_id_encode = rb_intern("encode");
	s_id_decode = rb_intern("decode");
	s_id_CFUNC = rb_intern("CFUNC");

	rb_cPG_Coder = rb_define_class_under( rb_mPG, "Coder", rb_cObject );
	rb_define_alloc_func( rb_cPG_Coder, pg_coder_allocate );
	rb_define_method( rb_cPG_Coder, "oid=", pg_coder_oid_set, 1 );
	rb_define_method( rb_cPG_Coder, "oid", pg_coder_oid_get, 0 );
	rb_define_method( rb_cPG_Coder, "format=", pg_coder_format_set, 1 );
	rb_define_method( rb_cPG_Coder, "format", pg_coder_format_get, 0 );
	rb_define_attr(   rb_cPG_Coder, "name", 1, 1 );
	rb_define_method( rb_cPG_Coder, "encode", pg_coder_encode, 1 );
	rb_define_method( rb_cPG_Coder, "decode", pg_coder_decode, -1 );

	rb_cPG_SimpleCoder = rb_define_class_under( rb_mPG, "SimpleCoder", rb_cPG_Coder );

	rb_cPG_SimpleEncoder = rb_define_class_under( rb_mPG, "SimpleEncoder", rb_cPG_SimpleCoder );
	rb_define_alloc_func( rb_cPG_SimpleEncoder, pg_simple_encoder_allocate );
	rb_cPG_SimpleDecoder = rb_define_class_under( rb_mPG, "SimpleDecoder", rb_cPG_SimpleCoder );
	rb_define_alloc_func( rb_cPG_SimpleDecoder, pg_simple_decoder_allocate );

	rb_cPG_CompositeCoder = rb_define_class_under( rb_mPG, "CompositeCoder", rb_cPG_Coder );
	rb_define_method( rb_cPG_CompositeCoder, "elements_type=", pg_coder_elements_type_set, 1 );
	rb_define_attr( rb_cPG_CompositeCoder, "elements_type", 1, 0 );
	rb_define_method( rb_cPG_CompositeCoder, "needs_quotation=", pg_coder_needs_quotation_set, 1 );
	rb_define_method( rb_cPG_CompositeCoder, "needs_quotation?", pg_coder_needs_quotation_get, 0 );
	rb_define_method( rb_cPG_CompositeCoder, "delimiter=", pg_coder_delimiter_set, 1 );
	rb_define_method( rb_cPG_CompositeCoder, "delimiter", pg_coder_delimiter_get, 0 );

	rb_cPG_CompositeEncoder = rb_define_class_under( rb_mPG, "CompositeEncoder", rb_cPG_CompositeCoder );
	rb_define_alloc_func( rb_cPG_CompositeEncoder, pg_composite_encoder_allocate );
	rb_cPG_CompositeDecoder = rb_define_class_under( rb_mPG, "CompositeDecoder", rb_cPG_CompositeCoder );
	rb_define_alloc_func( rb_cPG_CompositeDecoder, pg_composite_decoder_allocate );
}
