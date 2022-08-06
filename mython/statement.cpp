#include "statement.h"

#include <iostream>
#include <sstream>

using namespace std;

namespace ast {

    using runtime::Closure;
    using runtime::Context;
    using runtime::ObjectHolder;
    using runtime::ClassInstance;
    using runtime::Method;
    using runtime::Class;
    using runtime::String;
    using runtime::Bool;
    using runtime::Number;

    namespace {
        const string ADD_METHOD = "__add__"s;
        const string INIT_METHOD = "__init__"s;
    }  // namespace

    ObjectHolder Assignment::Execute(Closure& closure, Context& context) {
        closure[var_] = rv_->Execute(closure, context);
        return closure.at(var_);
    }

    Assignment::Assignment(std::string var, std::unique_ptr<Statement>&& rv)
        : var_(var), rv_(std::move(rv))
    {
    }

    VariableValue::VariableValue(const std::string& var_name)
        : ids_({ var_name })
    {
    }

    VariableValue::VariableValue(std::vector<std::string> dotted_ids)
        : ids_(dotted_ids)
    {
    }

    ObjectHolder VariableValue::Execute(Closure& closure, [[maybe_unused]] Context& context) {
        if (closure.count(ids_[0]) == 0) {
            string error_message = "unknown variable ";
            error_message += ids_[0];
            throw runtime_error(error_message);
        }
        ObjectHolder object = closure.at(ids_[0]);
        for (size_t i = 1; i < ids_.size(); ++i) {
            if (auto* tmp = object.TryAs<ClassInstance>()) {
                object = tmp->Fields()[ids_[i]];
            }
            else {
                string error_message = "unknown field ";
                error_message += ids_[i];
                throw runtime_error(error_message);
            }
        }
        return object;
    }

    unique_ptr<Print> Print::Variable(const std::string& name) {
        auto value_ptr = unique_ptr<VariableValue>(new VariableValue(name));
        return make_unique<Print>(move(value_ptr));
    }

    Print::Print(unique_ptr<Statement>&& argument)
    {
        args_.emplace_back(move(argument));
    }

    Print::Print(vector<unique_ptr<Statement>>&& args)
        : args_(move(args))
    {
    }

    ObjectHolder Print::Execute(Closure& closure, Context& context) {
        ostream& out(context.GetOutputStream());
        size_t args_count = args_.size();
        for (size_t i = 0; i < args_count; ++i) {
            if (ObjectHolder tmp = args_[i]->Execute(closure, context)) {
                tmp->Print(out, context);
            }
            else {
                out << "None"s;
            }
            if (i != args_count - 1) {
                out << " ";
            }
        }
        out << "\n";
        return ObjectHolder::None();
    }

    MethodCall::MethodCall(std::unique_ptr<Statement>&& object, std::string method,
        std::vector<std::unique_ptr<Statement>>&& args)
        : object_(move(object)), method_(move(method)), args_(move(args))
    {
    }

    ObjectHolder MethodCall::Execute(Closure& closure, Context& context) {
        vector<ObjectHolder> actual_args;
        for (const auto& arg : args_) {
            actual_args.push_back(arg->Execute(closure, context));
        }
        return object_->Execute(closure, context).TryAs<ClassInstance>()->Call(method_, actual_args, context);
    }

    ObjectHolder Stringify::Execute(Closure& closure, Context& context) {
        ObjectHolder object = argument_->Execute(closure, context);
        if (!object) {
            return ObjectHolder::Own(String("None"s));
        }
        ostringstream out;
        ClassInstance* instance = object.TryAs<ClassInstance>();
        if (instance && instance->HasMethod("__str__", 0)) {
            instance->Call("__str__", {}, context)->Print(out, context);
        }
        else {
            object->Print(out, context);
        }
        return ObjectHolder::Own(String(out.str()));
    }

    ObjectHolder Add::Execute(Closure& closure, Context& context) {
        ObjectHolder left = left_->Execute(closure, context);
        ObjectHolder right = right_->Execute(closure, context);

        if (Number* left_ptr = left.TryAs<Number>()) {
            if (Number* right_ptr = right.TryAs<Number>()) {
                return ObjectHolder::Own(Number(left_ptr->GetValue() + right_ptr->GetValue()));
            }
            throw runtime_error("invalid add operation");
        }

        if (String* left_ptr = left.TryAs<String>()) {
            if (String* right_ptr = right.TryAs<String>()) {
                string str = left_ptr->GetValue();
                str += right_ptr->GetValue();
                return ObjectHolder::Own(String(str));
            }
            throw runtime_error("invalid add operation");
        }

        if (ClassInstance* left_ptr = left.TryAs<ClassInstance>()) {
            if (left_ptr->HasMethod(ADD_METHOD, 1)) {
                return left_ptr->Call(ADD_METHOD, { right }, context);
            }
        }
        throw runtime_error("invalid add operation");
    }

    ObjectHolder Sub::Execute(Closure& closure, Context& context) {
        Number* left_number = left_->Execute(closure, context).TryAs<Number>();
        Number* right_number = right_->Execute(closure, context).TryAs<Number>();
        if (left_number && right_number) {
            return ObjectHolder::Own(Number(left_number->GetValue() - right_number->GetValue()));
        }
        throw runtime_error("invalid subtract operation");
    }

    ObjectHolder Mult::Execute(Closure& closure, Context& context) {
        Number* left_number = left_->Execute(closure, context).TryAs<Number>();
        Number* right_number = right_->Execute(closure, context).TryAs<Number>();
        if (left_number && right_number) {
            return ObjectHolder::Own(Number(left_number->GetValue() * right_number->GetValue()));
        }
        throw runtime_error("invalid mult operation");
    }

    ObjectHolder Div::Execute(Closure& closure, Context& context) {
        Number* left_number = left_->Execute(closure, context).TryAs<Number>();
        Number* right_number = right_->Execute(closure, context).TryAs<Number>();
        if (left_number && right_number) {
            if (right_number->GetValue() != 0) {
                return ObjectHolder::Own(Number(left_number->GetValue() / right_number->GetValue()));
            }
            throw runtime_error("division by zero");
        }
        throw runtime_error("invalid div operation");
    }

    ObjectHolder Compound::Execute(Closure& closure, Context& context) {
        for (const auto& statement : statements_) {
            statement->Execute(closure, context);
            if (closure.count("returned_value")) {
                return ObjectHolder::None();
            }
        }
        return ObjectHolder::None();
    }

    ObjectHolder Return::Execute(Closure& closure, Context& context) {
        closure["returned_value"] = statement_->Execute(closure, context);
        return ObjectHolder::None();
    }

    ClassDefinition::ClassDefinition(ObjectHolder cls)
        : cls_(cls)
    {
    }

    ObjectHolder ClassDefinition::Execute(Closure& closure, [[maybe_unused]] Context& context) {
        closure[cls_.TryAs<Class>()->GetName()] = cls_;
        return ObjectHolder::None();
    }

    FieldAssignment::FieldAssignment(VariableValue object, std::string field_name,
        std::unique_ptr<Statement>&& rv)
        : object_(object), field_name_(field_name), rv_(std::move(rv))
    {
    }

    ObjectHolder FieldAssignment::Execute(Closure& closure, Context& context) {
        Closure& fields = object_.Execute(closure, context).TryAs<ClassInstance>()->Fields();
        fields[field_name_] = rv_->Execute(closure, context);
        return fields.at(field_name_);
    }

    IfElse::IfElse(std::unique_ptr<Statement>&& condition, std::unique_ptr<Statement>&& if_body,
        std::unique_ptr<Statement>&& else_body)
        : condition_(move(condition)), if_body_(move(if_body)), else_body_(move(else_body))
    {
    }

    ObjectHolder IfElse::Execute(Closure& closure, Context& context) {
        if (IsTrue(condition_->Execute(closure, context))) {
            if_body_->Execute(closure, context);
        }
        else if (else_body_) {
            else_body_->Execute(closure, context);
        }
        return ObjectHolder::None();
    }

    ObjectHolder Or::Execute(Closure& closure, Context& context) {
        bool left = IsTrue(left_->Execute(closure, context));
        if (!left) {
            return ObjectHolder::Own(Bool(IsTrue(right_->Execute(closure, context))));
        }
        return ObjectHolder::Own(Bool(true));
    }

    ObjectHolder And::Execute(Closure& closure, Context& context) {
        bool left = IsTrue(left_->Execute(closure, context));
        if (left) {
            return ObjectHolder::Own(Bool(IsTrue(right_->Execute(closure, context))));
        }
        return ObjectHolder::Own(Bool(false));
    }

    ObjectHolder Not::Execute(Closure& closure, Context& context) {
        return ObjectHolder::Own(Bool(!IsTrue(argument_->Execute(closure, context))));
    }

    Comparison::Comparison(Comparator cmp, unique_ptr<Statement>&& lhs, unique_ptr<Statement>&& rhs)
        : BinaryOperation(std::move(lhs), std::move(rhs)), cmp_(cmp)
    {
    }

    ObjectHolder Comparison::Execute(Closure& closure, Context& context) {
        return ObjectHolder::Own(Bool(cmp_(left_->Execute(closure, context), right_->Execute(closure, context), context)));
    }

    NewInstance::NewInstance(const runtime::Class& class_, std::vector<std::unique_ptr<Statement>>&& args)
        : class_(class_), args_(move(args))
    {
    }

    NewInstance::NewInstance(const runtime::Class& class_)
        : class_(class_)
    {
    }

    ObjectHolder NewInstance::Execute(Closure& closure, Context& context) {
        ObjectHolder object = ObjectHolder::Own(ClassInstance(class_));
        const Method* method = class_.GetMethod(INIT_METHOD);
        if (!method || method->formal_params.size() != args_.size()) {
            return object;
        }
        vector<ObjectHolder> actual_args;
        for (const auto& arg : args_) {
            actual_args.push_back(arg->Execute(closure, context));
        }
        if (ObjectHolder after_init = object.TryAs<ClassInstance>()->Call(INIT_METHOD, actual_args, context)) {
            return after_init;
        }
        return object;
    }

    MethodBody::MethodBody(std::unique_ptr<Statement>&& body)
        : body_(move(body))
    {
    }

    ObjectHolder MethodBody::Execute(Closure& closure, Context& context) {
        body_->Execute(closure, context);
        if (closure.count("returned_value")) {
            return closure.at("returned_value");
        }
        return ObjectHolder::None();
    }

}  // namespace ast
