#include "benchmark/benchmark.h"

#include "google/api/expr/v1alpha1/syntax.pb.h"
#include "google/protobuf/text_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/strings/match.h"
#include "eval/public/activation.h"
#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "eval/public/structs/cel_proto_wrapper.h"
#include "eval/tests/request_context.pb.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace {

using google::api::expr::v1alpha1::Expr;
using google::api::expr::v1alpha1::SourceInfo;

// Benchmark test
// Evaluates cel expression:
// '1 + 1 + 1 .... +1'
static void BM_Eval(benchmark::State& state) {
  auto builder = CreateCelExpressionBuilder();
  auto reg_status = RegisterBuiltinFunctions(builder->GetRegistry());
  CHECK_OK(reg_status);

  int len = state.range(0);

  Expr root_expr;
  Expr* cur_expr = &root_expr;

  for (int i = 0; i < len; i++) {
    Expr::Call* call = cur_expr->mutable_call_expr();
    call->set_function("_+_");
    call->add_args()->mutable_const_expr()->set_int64_value(1);
    cur_expr = call->add_args();
  }

  cur_expr->mutable_const_expr()->set_int64_value(1);

  SourceInfo source_info;
  auto cel_expr_status = builder->CreateExpression(&root_expr, &source_info);
  CHECK_OK(cel_expr_status.status());

  std::unique_ptr<CelExpression> cel_expr = std::move(cel_expr_status.value());

  for (auto _ : state) {
    google::protobuf::Arena arena;
    Activation activation;
    auto eval_result = cel_expr->Evaluate(activation, &arena);
    CHECK_OK(eval_result.status());

    CelValue result = eval_result.value();
    GOOGLE_CHECK(result.IsInt64());
    GOOGLE_CHECK(result.Int64OrDie() == len + 1);
  }
}

BENCHMARK(BM_Eval)->Range(1, 32768);

std::string CELAstFlattenedMap() {
  return R"(
call_expr: <
  function: "_&&_"
  args: <
    call_expr: <
      function: "!_"
      args: <
        call_expr: <
          function: "@in"
          args: <
            ident_expr: <
              name: "ip"
            >
          >
          args: <
            list_expr: <
              elements: <
                const_expr: <
                  string_value: "10.0.1.4"
                >
              >
              elements: <
                const_expr: <
                  string_value: "10.0.1.5"
                >
              >
              elements: <
                const_expr: <
                  string_value: "10.0.1.6"
                >
              >
            >
          >
        >
      >
    >
  >
  args: <
    call_expr: <
      function: "_||_"
      args: <
        call_expr: <
          function: "_||_"
          args: <
            call_expr: <
              function: "_&&_"
              args: <
                call_expr: <
                  target: <
                    ident_expr: <
                      name: "path"
                    >
                  >
                  function: "startsWith"
                  args: <
                    const_expr: <
                      string_value: "v1"
                    >
                  >
                >
              >
              args: <
                call_expr: <
                  function: "@in"
                  args: <
                    ident_expr: <
                      name: "token"
                    >
                  >
                  args: <
                    list_expr: <
                      elements: <
                        const_expr: <
                          string_value: "v1"
                        >
                      >
                      elements: <
                        const_expr: <
                          string_value: "v2"
                        >
                      >
                      elements: <
                        const_expr: <
                          string_value: "admin"
                        >
                      >
                    >
                  >
                >
              >
            >
          >
          args: <
            call_expr: <
              function: "_&&_"
              args: <
                call_expr: <
                  target: <
                    ident_expr: <
                      name: "path"
                    >
                  >
                  function: "startsWith"
                  args: <
                    const_expr: <
                      string_value: "v2"
                    >
                  >
                >
              >
              args: <
                call_expr: <
                  function: "@in"
                  args: <
                    ident_expr: <
                      name: "token"
                    >
                  >
                  args: <
                    list_expr: <
                      elements: <
                        const_expr: <
                          string_value: "v2"
                        >
                      >
                      elements: <
                        const_expr: <
                          string_value: "admin"
                        >
                      >
                    >
                  >
                >
              >
            >
          >
        >
      >
      args: <
        call_expr: <
          function: "_&&_"
          args: <
            call_expr: <
              function: "_&&_"
              args: <
                call_expr: <
                  target: <
                    ident_expr: <
                      name: "path"
                    >
                  >
                  function: "startsWith"
                  args: <
                    const_expr: <
                      string_value: "/admin"
                    >
                  >
                >
              >
              args: <
                call_expr: <
                  function: "_==_"
                  args: <
                    ident_expr: <
                      name: "token"
                    >
                  >
                  args: <
                    const_expr: <
                      string_value: "admin"
                    >
                  >
                >
              >
            >
          >
          args: <
            call_expr: <
              function: "@in"
              args: <
                ident_expr: <
                  name: "ip"
                >
              >
              args: <
                list_expr: <
                  elements: <
                    const_expr: <
                      string_value: "10.0.1.1"
                    >
                  >
                  elements: <
                    const_expr: <
                      string_value: "10.0.1.2"
                    >
                  >
                  elements: <
                    const_expr: <
                      string_value: "10.0.1.3"
                    >
                  >
                >
              >
            >
          >
        >
      >
    >
  >
>
)";
}

// This proto is obtained from CELAstFlattenedMap by replacing "ip", "token",
// and "path" idents with selector expressions for "request.ip",
// "request.token", and "request.path".
std::string CELAst() {
  return R"(
call_expr: <
  function: "_&&_"
  args: <
    call_expr: <
      function: "!_"
      args: <
        call_expr: <
          function: "@in"
          args: <
            select_expr: <
              operand: <
                ident_expr: <
                  name: "request"
                >
              >
              field: "ip"
            >
          >
          args: <
            list_expr: <
              elements: <
                const_expr: <
                  string_value: "10.0.1.4"
                >
              >
              elements: <
                const_expr: <
                  string_value: "10.0.1.5"
                >
              >
              elements: <
                const_expr: <
                  string_value: "10.0.1.6"
                >
              >
            >
          >
        >
      >
    >
  >
  args: <
    call_expr: <
      function: "_||_"
      args: <
        call_expr: <
          function: "_||_"
          args: <
            call_expr: <
              function: "_&&_"
              args: <
                call_expr: <
                  target: <
                    select_expr: <
                      operand: <
                        ident_expr: <
                          name: "request"
                        >
                      >
                      field: "path"
                    >
                  >
                  function: "startsWith"
                  args: <
                    const_expr: <
                      string_value: "v1"
                    >
                  >
                >
              >
              args: <
                call_expr: <
                  function: "@in"
                  args: <
                    select_expr: <
                      operand: <
                        ident_expr: <
                          name: "request"
                        >
                      >
                      field: "token"
                    >
                  >
                  args: <
                    list_expr: <
                      elements: <
                        const_expr: <
                          string_value: "v1"
                        >
                      >
                      elements: <
                        const_expr: <
                          string_value: "v2"
                        >
                      >
                      elements: <
                        const_expr: <
                          string_value: "admin"
                        >
                      >
                    >
                  >
                >
              >
            >
          >
          args: <
            call_expr: <
              function: "_&&_"
              args: <
                call_expr: <
                  target: <
                    select_expr: <
                      operand: <
                        ident_expr: <
                          name: "request"
                        >
                      >
                      field: "path"
                    >
                  >
                  function: "startsWith"
                  args: <
                    const_expr: <
                      string_value: "v2"
                    >
                  >
                >
              >
              args: <
                call_expr: <
                  function: "@in"
                  args: <
                    select_expr: <
                      operand: <
                        ident_expr: <
                          name: "request"
                        >
                      >
                      field: "token"
                    >
                  >
                  args: <
                    list_expr: <
                      elements: <
                        const_expr: <
                          string_value: "v2"
                        >
                      >
                      elements: <
                        const_expr: <
                          string_value: "admin"
                        >
                      >
                    >
                  >
                >
              >
            >
          >
        >
      >
      args: <
        call_expr: <
          function: "_&&_"
          args: <
            call_expr: <
              function: "_&&_"
              args: <
                call_expr: <
                  target: <
                    select_expr: <
                      operand: <
                        ident_expr: <
                          name: "request"
                        >
                      >
                      field: "path"
                    >
                  >
                  function: "startsWith"
                  args: <
                    const_expr: <
                      string_value: "/admin"
                    >
                  >
                >
              >
              args: <
                call_expr: <
                  function: "_==_"
                  args: <
                    select_expr: <
                      operand: <
                        ident_expr: <
                          name: "request"
                        >
                      >
                      field: "token"
                    >
                  >
                  args: <
                    const_expr: <
                      string_value: "admin"
                    >
                  >
                >
              >
            >
          >
          args: <
            call_expr: <
              function: "@in"
              args: <
                select_expr: <
                  operand: <
                    ident_expr: <
                      name: "request"
                    >
                  >
                  field: "ip"
                >
              >
              args: <
                list_expr: <
                  elements: <
                    const_expr: <
                      string_value: "10.0.1.1"
                    >
                  >
                  elements: <
                    const_expr: <
                      string_value: "10.0.1.2"
                    >
                  >
                  elements: <
                    const_expr: <
                      string_value: "10.0.1.3"
                    >
                  >
                >
              >
            >
          >
        >
      >
    >
  >
>
)";
}

const char kIP[] = "10.0.1.2";
const char kPath[] = "/admin/edit";
const char kToken[] = "admin";

ABSL_ATTRIBUTE_NOINLINE
bool NativeCheck(std::map<std::string, std::string>& attributes,
                 const std::unordered_set<std::string>& blacklists,
                 const std::unordered_set<std::string>& whitelists) {
  auto& ip = attributes["ip"];
  auto& path = attributes["path"];
  auto& token = attributes["token"];
  if (blacklists.find(ip) != blacklists.end()) {
    return false;
  }
  if (absl::StartsWith(path, "v1")) {
    if (token == "v1" || token == "v2" || token == "admin") {
      return true;
    }
  } else if (absl::StartsWith(path, "v2")) {
    if (token == "v2" || token == "admin") {
      return true;
    }
  } else if (absl::StartsWith(path, "/admin")) {
    if (token == "admin") {
      if (whitelists.find(ip) != whitelists.end()) {
        return true;
      }
    }
  }
  return false;
}

void BM_PolicyNative(benchmark::State& state) {
  const auto blacklists =
      std::unordered_set<std::string>{"10.0.1.4", "10.0.1.5", "10.0.1.6"};
  const auto whitelists =
      std::unordered_set<std::string>{"10.0.1.1", "10.0.1.2", "10.0.1.3"};
  auto attributes = std::map<std::string, std::string>{
      {"ip", kIP}, {"token", kToken}, {"path", kPath}};
  for (auto _ : state) {
    auto result = NativeCheck(attributes, blacklists, whitelists);
    GOOGLE_CHECK(result);
  }
}

BENCHMARK(BM_PolicyNative);

/*
  Evaluates an expression:

  !(ip in ["10.0.1.4", "10.0.1.5", "10.0.1.6"]) &&
   (
    (path.startsWith("v1") && token in ["v1", "v2", "admin"]) ||
    (path.startsWith("v2") && token in ["v2", "admin"]) ||
    (path.startsWith("/admin") && token == "admin" && ip in ["10.0.1.1",
  "10.0.1.2", "10.0.1.3"])
   )
*/
void BM_PolicySymbolic(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  google::protobuf::TextFormat::ParseFromString(CELAstFlattenedMap(), &expr);

  InterpreterOptions options;
  options.constant_folding = true;
  options.constant_arena = &arena;

  auto builder = CreateCelExpressionBuilder(options);
  CHECK_OK(RegisterBuiltinFunctions(builder->GetRegistry()));

  SourceInfo source_info;
  auto cel_expression_status = builder->CreateExpression(&expr, &source_info);
  CHECK_OK(cel_expression_status.status());

  auto cel_expression = std::move(cel_expression_status.value());
  Activation activation;
  activation.InsertValue("ip", CelValue::CreateStringView(kIP));
  activation.InsertValue("path", CelValue::CreateStringView(kPath));
  activation.InsertValue("token", CelValue::CreateStringView(kToken));

  for (auto _ : state) {
    auto eval_result = cel_expression->Evaluate(activation, &arena);
    CHECK_OK(eval_result.status());
    GOOGLE_CHECK(eval_result.value().BoolOrDie());
  }
}

BENCHMARK(BM_PolicySymbolic);

class RequestMap : public CelMap {
 public:
  absl::optional<CelValue> operator[](CelValue key) const override {
    if (!key.IsString()) {
      return {};
    }
    auto value = key.StringOrDie().value();
    if (value == "ip") {
      return CelValue::CreateStringView(kIP);
    } else if (value == "path") {
      return CelValue::CreateStringView(kPath);
    } else if (value == "token") {
      return CelValue::CreateStringView(kToken);
    }
    return {};
  }
  int size() const override { return 3; }
  const CelList* ListKeys() const override { return nullptr; }
};

// Uses a lazily constructed map container for "ip", "path", and "token".
void BM_PolicySymbolicMap(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  google::protobuf::TextFormat::ParseFromString(CELAst(), &expr);

  auto builder = CreateCelExpressionBuilder();
  CHECK_OK(RegisterBuiltinFunctions(builder->GetRegistry()));

  SourceInfo source_info;
  auto cel_expression_status = builder->CreateExpression(&expr, &source_info);
  CHECK_OK(cel_expression_status.status());

  auto cel_expression = std::move(cel_expression_status.value());
  Activation activation;
  RequestMap request;
  activation.InsertValue("request", CelValue::CreateMap(&request));

  for (auto _ : state) {
    auto eval_result = cel_expression->Evaluate(activation, &arena);
    CHECK_OK(eval_result.status());
    GOOGLE_CHECK(eval_result.value().BoolOrDie());
  }
}

BENCHMARK(BM_PolicySymbolicMap);

// Uses a protobuf container for "ip", "path", and "token".
void BM_PolicySymbolicProto(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  google::protobuf::TextFormat::ParseFromString(CELAst(), &expr);

  auto builder = CreateCelExpressionBuilder();
  CHECK_OK(RegisterBuiltinFunctions(builder->GetRegistry()));

  SourceInfo source_info;
  auto cel_expression_status = builder->CreateExpression(&expr, &source_info);
  CHECK_OK(cel_expression_status.status());

  auto cel_expression = std::move(cel_expression_status.value());
  Activation activation;
  RequestContext request;
  request.set_ip(kIP);
  request.set_path(kPath);
  request.set_token(kToken);
  activation.InsertValue("request",
                         CelProtoWrapper::CreateMessage(&request, &arena));

  for (auto _ : state) {
    auto eval_result = cel_expression->Evaluate(activation, &arena);
    CHECK_OK(eval_result.status());
    GOOGLE_CHECK(eval_result.value().BoolOrDie());
  }
}

BENCHMARK(BM_PolicySymbolicProto);

constexpr char kListSum[] = R"(
id: 1
comprehension_expr: <
  accu_var: "__result__"
  iter_var: "x"
  iter_range: <
    id: 2
    ident_expr: <
      name: "list"
    >
  >
  accu_init: <
    id: 3
    const_expr: <
      int64_value: 0
    >
  >
  loop_step: <
    id: 4
    call_expr: <
      function: "_+_"
      args: <
        id: 5
        ident_expr: <
          name: "__result__"
        >
      >
      args: <
        id: 6
        ident_expr: <
          name: "x"
        >
      >
    >
  >
  loop_condition: <
    id: 7
    const_expr: <
      bool_value: true
    >
  >
  result: <
    id: 8
    ident_expr: <
      name: "__result__"
    >
  >
>)";

void BM_Comprehension(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  Activation activation;
  GOOGLE_CHECK(google::protobuf::TextFormat::ParseFromString(kListSum, &expr));

  int len = state.range(0);
  std::vector<CelValue> list;
  list.reserve(len);
  for (int i = 0; i < len; i++) {
    list.push_back(CelValue::CreateInt64(1));
  }

  ContainerBackedListImpl cel_list(std::move(list));
  activation.InsertValue("list", CelValue::CreateList(&cel_list));
  auto builder = CreateCelExpressionBuilder();
  CHECK_OK(RegisterBuiltinFunctions(builder->GetRegistry()));
  auto expr_plan = builder->CreateExpression(&expr, nullptr);
  CHECK_OK(expr_plan.status());
  for (auto _ : state) {
    auto result = expr_plan.value()->Evaluate(activation, &arena);
    CHECK_OK(result.status());
    GOOGLE_CHECK(result->IsInt64());
    CHECK_EQ(result->Int64OrDie(), len);
  }
}

BENCHMARK(BM_Comprehension)->Range(1, 1 << 20);

// Sum a square with a nested comprehension
constexpr char kNestedListSum[] = R"(
id: 1
comprehension_expr: <
  accu_var: "__result__"
  iter_var: "x"
  iter_range: <
    id: 2
    ident_expr: <
      name: "list"
    >
  >
  accu_init: <
    id: 3
    const_expr: <
      int64_value: 0
    >
  >
  loop_step: <
    id: 4
    call_expr: <
      function: "_+_"
      args: <
        id: 5
        ident_expr: <
          name: "__result__"
        >
      >
      args: <
        id: 6
        comprehension_expr: <
          accu_var: "__result__"
          iter_var: "x"
          iter_range: <
            id: 9
            ident_expr: <
              name: "list"
            >
          >
          accu_init: <
            id: 10
            const_expr: <
              int64_value: 0
            >
          >
          loop_step: <
            id: 11
            call_expr: <
              function: "_+_"
              args: <
                id: 12
                ident_expr: <
                  name: "__result__"
                >
              >
              args: <
                id: 13
                ident_expr: <
                  name: "x"
                >
              >
            >
          >
          loop_condition: <
            id: 14
            const_expr: <
              bool_value: true
            >
          >
          result: <
            id: 15
            ident_expr: <
              name: "__result__"
            >
          >
        >
      >
    >
  >
  loop_condition: <
    id: 7
    const_expr: <
      bool_value: true
    >
  >
  result: <
    id: 8
    ident_expr: <
      name: "__result__"
    >
  >
>)";

void BM_NestedComprehension(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  Activation activation;
  GOOGLE_CHECK(google::protobuf::TextFormat::ParseFromString(kNestedListSum, &expr));

  int len = state.range(0);
  std::vector<CelValue> list;
  list.reserve(len);
  for (int i = 0; i < len; i++) {
    list.push_back(CelValue::CreateInt64(1));
  }

  ContainerBackedListImpl cel_list(std::move(list));
  activation.InsertValue("list", CelValue::CreateList(&cel_list));
  auto builder = CreateCelExpressionBuilder();
  CHECK_OK(RegisterBuiltinFunctions(builder->GetRegistry()));
  auto expr_plan = builder->CreateExpression(&expr, nullptr);
  CHECK_OK(expr_plan.status());
  for (auto _ : state) {
    auto result = expr_plan.value()->Evaluate(activation, &arena);
    CHECK_OK(result.status());
    GOOGLE_CHECK(result->IsInt64());
    CHECK_EQ(result->Int64OrDie(), len * len);
  }
}

BENCHMARK(BM_NestedComprehension)->Range(1, 1 << 10);

void BM_ComprehensionCpp(benchmark::State& state) {
  google::protobuf::Arena arena;
  Expr expr;
  Activation activation;

  int len = state.range(0);
  std::vector<CelValue> list;
  list.reserve(len);
  for (int i = 0; i < len; i++) {
    list.push_back(CelValue::CreateInt64(1));
  }

  auto op = [&list]() {
    int sum = 0;
    for (const auto& value : list) {
      sum += value.Int64OrDie();
    }
    return sum;
  };
  for (auto _ : state) {
    int result = op();
    CHECK_EQ(result, len);
  }
}

BENCHMARK(BM_ComprehensionCpp)->Range(1, 1 << 20);

}  // namespace

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
