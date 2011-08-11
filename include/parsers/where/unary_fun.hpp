#pragma once

#include <parsers/where/expression_ast.hpp>
#include <parsers/operators.hpp>

namespace parsers {
	namespace where {

		struct unary_fun {
			//filter_handler_interface::bound_function_type e_fn;
			boost::shared_ptr<binary_function_impl> i_fn;
			unsigned int function_id;
			std::wstring name;
			expression_ast subject;

			unary_fun(std::wstring name, expression_ast const& subject) : name(name), subject(subject), function_id(0) {}

			expression_ast evaluate(filter_handler handler, value_type type) const;

			bool bind(value_type type, filter_handler handler);
			bool is_transparent(value_type type);
			bool is_bound() const;
		private:
			unary_fun() : function_id(0) {}
		};
	}
}
