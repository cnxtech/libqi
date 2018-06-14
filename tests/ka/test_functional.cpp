#include <atomic>
#include <cmath>
#include <cstdlib>
#include <future>
#include <limits>
#include <list>
#include <sstream>
#include <string>
#include <vector>
#include <boost/optional.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <gtest/gtest.h>
#include "test_functional_common.hpp"
#include <ka/algorithm.hpp>
#include <ka/conceptpredicate.hpp>
#include <ka/functional.hpp>
#include <ka/memory.hpp>
#include <ka/mutablestore.hpp>
#include <ka/mutex.hpp>
#include <ka/range.hpp>
#include <ka/testutils.hpp>
#include <ka/typetraits.hpp>
#include <ka/utility.hpp>

TEST(FunctionalPolymorphicConstantFunction, RegularNonVoid) {
  using namespace ka;
  using F = poly_constant_function<int>;
  auto const incr = [](F& f) {
    ++f.ret;
  };
  // F is regular because int is.
  ASSERT_TRUE(is_regular(bounded_range(F{0}, F{100}, incr)));
}

TEST(FunctionalPolymorphicConstantFunction, RegularVoid) {
  using namespace ka;
  using F = poly_constant_function<void>;
  ASSERT_TRUE(is_regular({F{}}));
}

struct non_regular_t {
  int i;
  friend bool operator==(non_regular_t a, non_regular_t b) {
    return &a == &b;
  }
  friend bool operator<(non_regular_t a, non_regular_t b) {
    return &a < &b;
  }
};

TEST(FunctionalPolymorphicConstantFunction, NonRegularNonVoid) {
  using namespace ka;
  using F = poly_constant_function<non_regular_t>;
  auto incr = [](F& f) {
    ++f.ret.i;
  };
  // F is not regular because non_regular_t isn't.
  ASSERT_FALSE(is_regular(bounded_range(F{{0}}, F{{100}}, incr)));
}

TEST(FunctionalPolymorphicConstantFunction, BasicNonVoid) {
  using namespace ka;
  char const c = 'z';
  poly_constant_function<unsigned char> f{c};
  ASSERT_EQ(c, f());
  ASSERT_EQ(c, f(1));
  ASSERT_EQ(c, f(2.345));
  ASSERT_EQ(c, f("abcd"));
  ASSERT_EQ(c, f(true));
  ASSERT_EQ(c, f(std::vector<int>{5, 7, 2, 1}));
  ASSERT_EQ(c, f(1, 2.345, "abcd", true));
}

TEST(FunctionalPolymorphicConstantFunction, BasicVoid) {
  using namespace ka;
  poly_constant_function<void> f;
  ASSERT_NO_THROW(f());
  ASSERT_NO_THROW(f(1));
  ASSERT_NO_THROW(f(2.345));
  ASSERT_NO_THROW(f("abcd"));
  ASSERT_NO_THROW(f(true));
  ASSERT_NO_THROW(f(std::vector<int>{5, 7, 2, 1}));
  ASSERT_NO_THROW(f(1, 2.345, "abcd", true));
}

namespace {
  // For use to test `is_regular` only.
  // The returned strings are irrelevant, the only point is that these functions
  // are regular.
  std::string strbool0(bool x) {
    return x ? "test test" : "1, 2, 1, 2";
  }

  std::string strbool1(bool x) {
    return x ? "mic mic" : "Vous etes chauds ce soir?!";
  }
}

TEST(FunctionalCompose, Regular) {
  using namespace ka;
  using namespace std;
  using C = composition<string (*)(bool), bool (*)(float)>;
  ASSERT_TRUE(is_regular({
    C{strbool0, isnan}, C{strbool0, isfinite}, C{strbool1, isinf}
  }));
}

TEST(FunctionalCompose, NonVoid) {
  using namespace ka;

  auto half = [](int x) {
    return x / 2.f;
  };
  auto greater_1 = [](float x) {
    return x > 1.f;
  };
  auto half_greater_1 = compose(greater_1, half);
  static_assert(traits::Equal<bool, decltype(half_greater_1(3))>::value, "");

  ASSERT_TRUE(half_greater_1(3));
  ASSERT_FALSE(half_greater_1(1));
}

TEST(FunctionalCompose, Void) {
  using namespace ka;
  using namespace ka::traits;
  int const uninitialized = std::numeric_limits<int>::max();
  int order = 0;
  int fOrder = uninitialized;
  int gOrder = uninitialized;
  auto f = [&](int) {
    fOrder = order++;
  };
  auto g = [&] {
    gOrder = order++;
  };
  auto k = compose(g, f);
  static_assert(Equal<void, decltype(k(3))>::value, "");

  ASSERT_EQ(uninitialized, fOrder);
  ASSERT_EQ(uninitialized, gOrder);
  k(3);
  ASSERT_EQ(0, fOrder);
  ASSERT_EQ(1, gOrder);
}

TEST(FunctionalCompose, Multi) {
  using namespace ka;
  using namespace ka::traits;
  using std::string;

  auto half = [](int x) {
    return x / 2.f;
  };
  auto greater_1 = [](float x) {
    return x > 1.f;
  };
  auto str = [](bool x) -> string {
    return x ? "true" : "false";
  };

  auto f = compose(str, compose(greater_1, half));
  static_assert(Equal<string, decltype(f(3))>::value, "");

  ASSERT_EQ("true", f(3));
  ASSERT_EQ("false", f(1));
}

TEST(FunctionalCompose, Retraction) {
  using namespace ka;
  using namespace ka::traits;
  using namespace test;
  // We compose a function and its retraction and expect to get the identity
  // function.
  f_t f;
  auto g = retract(f);
  auto gf = compose(g, f);
  ASSERT_EQ(e0_t::a, gf(e0_t::a));
  ASSERT_EQ(e0_t::b, gf(e0_t::b));

// TODO: Remove this define (but keep the content) when get rid of VS2013.
#if !KA_COMPILER_VS2013_OR_BELOW
  static_assert(Equal<decltype(gf), id_transfo>::value, "");
#endif
}

TEST(FunctionalCompose, SeemsRetractionButNotQuite) {
  using namespace ka;
  using namespace ka::traits;
  using namespace test;
  // Even if the two functions have retractions (even, are isomorphisms), and
  // can be composed (right domain and codomain), `g_inv_t` is _not_ a retraction
  // for `F`.
  // We expect to _not_ get the identity function.
  auto ginv_f = compose(g_inv_t{}, f_t{});
  ASSERT_EQ(e0_t::b, ginv_f(e0_t::a));
  ASSERT_EQ(e0_t::a, ginv_f(e0_t::b));
  static_assert(!Equal<decltype(ginv_f), id_transfo>::value, "");
}

TEST(FunctionalCompose, Identity) {
  using namespace ka;
  using namespace ka::traits;
  using namespace ka::functional_ops;
  using namespace test;
  f_t f;
  id_transfo _1;
  static_assert(Equal<Decay<decltype(_1 * _1)>, decltype(_1)>::value, "");
  static_assert(Equal<Decay<decltype(f * _1)>, decltype(f)>::value, "");
  static_assert(Equal<Decay<decltype(f * _1 * _1)>, decltype(f)>::value, "");
  static_assert(Equal<Decay<decltype(_1 * f)>, decltype(f)>::value, "");
  static_assert(Equal<Decay<decltype(_1 * f * _1)>, decltype(f)>::value, "");

// TODO: Remove this define (but keep the content) when get rid of VS2013.
#if KA_COMPILER_VS2013_OR_BELOW
  // To avoid "unreferenced local variable" warnings.
  f(test::e0_t::a);
  _1(test::e0_t::a);
#endif
}

TEST(FunctionalCompose, Simplification) {
// TODO: Remove this define (but keep the content) when get rid of VS2013.
// VS2013 compiler does not allow to do composition simplifications.
#if !KA_COMPILER_VS2013_OR_BELOW
  using namespace ka;
  using namespace ka::traits;
  using namespace ka::functional_ops;
  using namespace test;
  // We expect chains of composition to be simplified in the right way.
  f_t f;
  auto g = retract(f);
  auto z = g * f * g * f * g * f * g * f;
  static_assert(Equal<decltype(z), id_transfo>::value, "");
  static_assert(Equal<Decay<decltype(z * g)>, decltype(g)>::value, "");
#endif
}

TEST(FunctionalCompose, Associative) {
  using namespace ka::functional_ops;
  using std::string;

  auto f = [](int x) {
    return x / 2.f;
  };
  auto g = [](float x) {
    return x > 1.f;
  };
  auto h = [](bool x) -> string {
    return x ? "true" : "false";
  };
  auto i = [](string const& x) -> std::size_t {
    return x.size();
  };

  auto a = ((i * h) * g) * f;
  auto b = (i * (h * g)) * f;
  auto c = i * (h * (g * f));
  auto d = (i * h) * (g * f);
  auto e = i * ((h * g) * f);

  ASSERT_EQ(a(3), b(3));
  ASSERT_EQ(b(3), c(3));
  ASSERT_EQ(c(3), d(3));
  ASSERT_EQ(d(3), e(3));

  ASSERT_EQ(a(0), b(0));
  ASSERT_EQ(b(0), c(0));
  ASSERT_EQ(c(0), d(0));
  ASSERT_EQ(d(0), e(0));
}

TEST(FunctionalCompose, Id) {
  using namespace ka;
  using namespace ka::functional_ops;
  using std::string;

  auto f = [](int x) {
    return x / 2.f;
  };
  auto g = [](float x) {
    return x > 1.f;
  };
  id_transfo _1;

  auto f0 = f * _1;
  auto f1 = _1 * f;
  auto gf0 = (g * f) * _1;
  auto gf1 = _1 * (g * f);

  ASSERT_EQ(f0(3), f1(3));
  ASSERT_EQ(gf0(3), gf1(3));

  ASSERT_EQ(f0(0), f1(0));
  ASSERT_EQ(gf0(0), gf1(0));
}

namespace {
  void remove_n(std::string& s, char c, int n) {
    ka::erase_if(s, [&](char x) {return x == c && --n >= 0;});
  }

  void concat(std::string& s, char c, int n) {
    // Precondition: n >= 0
    s.insert(s.end(), n, c);
  }

  void noop(std::string&, char, int) {
  }
}

TEST(FunctionalComposeAccu, Regular) {
  using namespace ka;
  using A = void (*)(std::string&, char, int);
  using C = composition_accu<A, A>;
  ASSERT_TRUE(is_regular({
    C{remove_n, concat}, C{concat, remove_n}, C{remove_n, remove_n}, C{concat, noop}
  }));
}

TEST(FunctionalComposeAccu, Multi) {
  using namespace ka;

  auto half = [](float& x) {
    x /= 2.f;
  };
  auto clamp = [](float& x) {
    if (x > 1.f) x = 1.f;
    if (x < -1.f) x = -1.f;
  };
  auto abs = [](float& x) {
    if (x < 0.f) x = -x;
  };

  auto f = compose_accu(abs, compose_accu(clamp, half));

  {
    float i = -3.f;
    f(i);
    ASSERT_EQ(1.f, i);
  } {
    float i = 1.f;
    f(i);
    ASSERT_EQ(0.5f, i);
  }
}

TEST(FunctionalComposeAccu, Retraction) {
  using namespace ka;
  using namespace ka::traits;
  using namespace test;
  // We compose an action and its retraction and expect to get the identity
  // action.
  a_t f;
  auto g = retract(f);
  auto gf = compose_accu(g, f);
  {
    e0_t e = e0_t::a;
    gf(e);
    ASSERT_EQ(e0_t::a, e);
  } {
    e0_t e = e0_t::b;
    gf(e);
    ASSERT_EQ(e0_t::b, e);
  }
  static_assert(Equal<decltype(gf), id_action>::value, "");
}

TEST(FunctionalComposeAccu, ComposeAccu) {
  using namespace ka;
  {
    auto a = compose_accu(remove_n, concat);
    std::string s{"youpi les amis"};
    a(s, ' ', 2);
    ASSERT_EQ(std::string{"youpilesamis  "}, s);
  } {
    auto a = compose_accu(concat, remove_n);
    std::string s{"youpi les amis"};
    a(s, ' ', 4);
    ASSERT_EQ(std::string{"youpilesamis    "}, s);
  } {
    auto a = compose_accu(concat, noop);
    std::string s{"youpi les amis"};
    a(s, '!', 3);
    ASSERT_EQ(std::string{"youpi les amis!!!"}, s);
  }
}

namespace {
  void drop3(std::string& s) {
    s.erase(0, 3);
  }

  void twice(std::string& s) {
    s += s;
  }
}

TEST(FunctionalComposeAccu, ComposeAction) {
  using namespace ka;
  {
    auto a = compose_accu(drop3, twice);
    std::string s{"youpi"};
    a(s);
    ASSERT_EQ(std::string{"piyoupi"}, s);
  } {
    auto a = compose_accu(twice, drop3);
    std::string s{"youpi"};
    a(s);
    ASSERT_EQ(std::string{"pipi"}, s);
  }
}

TEST(FunctionalComposeAccu, Identity) {
  using namespace ka;
  using namespace ka::traits;
  using namespace ka::functional_ops;
  using namespace test;
  a_t f;
  id_action _1;
  static_assert(Equal<Decay<decltype(_1 *= _1)>, decltype(_1)>::value, "");
  static_assert(Equal<Decay<decltype(f *= _1)>, decltype(f)>::value, "");
  static_assert(Equal<Decay<decltype(f *= _1 *= _1)>, decltype(f)>::value, "");
  static_assert(Equal<Decay<decltype(_1 *= f)>, decltype(f)>::value, "");
  static_assert(Equal<Decay<decltype(_1 *= f *= _1)>, decltype(f)>::value, "");

// TODO: Remove this define (but keep the content) when get rid of VS2013.
#if KA_COMPILER_VS2013_OR_BELOW
  // To avoid "unreferenced local variable" warnings.
  e0_t e = e0_t::a;
  f(e);
  _1(e);
#endif
}

TEST(FunctionalComposeAccu, Simplification) {
  using namespace ka;
  using namespace ka::traits;
  using namespace ka::functional_ops;
  using namespace test;
  // We expect chains of composition to be simplified in the right way.
  a_t f;
  auto g = retract(f);
  auto z = g *= f *= g *= f *= g *= f *= g *= f;
  static_assert(Equal<decltype(z), id_action>::value, "");
  static_assert(Equal<Decay<decltype(z *= g)>, decltype(g)>::value, "");
}

TEST(FunctionalComposeAccu, Associative) {
  using namespace ka::functional_ops;
  using std::string;

  auto f = [](float& x) {
    x /= 2.f;
  };
  auto g = [](float& x) {
    x = -x;
  };
  auto h = [](float& x) {
    x += x;
  };
  auto i = [](float& x) {
    x -= 1;
  };

  auto a = (((i *= h) *= g) *= f);
  auto b = ((i *= (h *= g)) *= f);
  auto c = (i *= (h *= (g *= f)));
  auto d = ((i *= h) *= (g *= f));
  auto e = (i *= ((h *= g) *= f));

  {
    float i = 3;
    float j = 3;
    a(i);
    b(j);
    ASSERT_EQ(i, j);
  } {
    float i = 3;
    float j = 3;
    b(i);
    c(j);
    ASSERT_EQ(i, j);
  } {
    float i = 3;
    float j = 3;
    c(i);
    d(j);
    ASSERT_EQ(i, j);
  } {
    float i = 3;
    float j = 3;
    d(i);
    e(j);
    ASSERT_EQ(i, j);
  } {
    float i = 0;
    float j = 0;
    a(i);
    b(j);
    ASSERT_EQ(i, j);
  } {
    float i = 0;
    float j = 0;
    b(i);
    c(j);
    ASSERT_EQ(i, j);
  } {
    float i = 0;
    float j = 0;
    c(i);
    d(j);
    ASSERT_EQ(i, j);
  } {
    float i = 0;
    float j = 0;
    d(i);
    e(j);
    ASSERT_EQ(i, j);
  }
}

TEST(FunctionalComposeAccu, Id) {
  using namespace ka;
  using namespace ka::functional_ops;
  using std::string;

  auto f = [](float& x) {
    x /= 2.f;
  };
  auto g = [](float& x) {
    x = -x;
  };
  id_action _1;

  auto f0 = (f *= _1);
  auto f1 = (_1 *= f);
  auto gf0 = ((g *= f) *= _1);
  auto gf1 = (_1 *= (g *= f));

  {
    float i = 3;
    float j = 3;
    f0(i);
    f1(j);
    ASSERT_EQ(i, j);
  } {
    float i = 3;
    float j = 3;
    gf0(i);
    gf1(j);
    ASSERT_EQ(i, j);
  } {
    float i = 0;
    float j = 0;
    f0(i);
    f1(j);
    ASSERT_EQ(i, j);
  } {
    float i = 0;
    float j = 0;
    gf0(i);
    gf1(j);
    ASSERT_EQ(i, j);
  }
}

namespace {
  struct x_t {
    bool b;
    bool operator==(x_t x) const {return b == x.b;}
  };

  template<typename T>
  struct constant_unit_t {
    T operator()() const {
      return T{};
    }
  };

  template<typename T>
  bool equal(T const& a, T const& b) {
    return a == b;
  }
}

using types = testing::Types<
  x_t, boost::optional<bool>, std::list<bool>
>;

template<typename T>
struct FunctionalSemiLift0 : testing::Test
{
};

TYPED_TEST_CASE(FunctionalSemiLift0, types);

TYPED_TEST(FunctionalSemiLift0, NonVoidCodomain) {
  using namespace ka;
  using T = TypeParam;

  auto positive = [](int i) {
    return i > 0;
  };
  auto unit = [](bool b) {
    return T{b};
  };
  auto f = semilift(positive, unit);

  static_assert(traits::Equal<T, decltype(f(0))>::value, "");
  ASSERT_TRUE(equal(T{true}, f(1)));
  ASSERT_TRUE(equal(T{false}, f(-1)));
}

using void_types = testing::Types<
  x_t, boost::optional<bool>, std::list<bool>
>;

template<typename T>
struct FunctionalSemiLift1 : testing::Test {
};

TYPED_TEST_CASE(FunctionalSemiLift1, void_types);

TYPED_TEST(FunctionalSemiLift1, VoidCodomain) {
  using namespace ka;
  using namespace ka::traits;
  using T = TypeParam;

  auto noop = [](int) {
  };
  constant_unit_t<T> unit;
  auto f = semilift(noop, unit);

  static_assert(Equal<T, decltype(f(0))>::value, "");
  ASSERT_TRUE(equal(unit(), f(0)));
}

TYPED_TEST(FunctionalSemiLift1, VoidCodomainVoidDomain) {
  using namespace ka;
  using namespace ka::traits;
  using T = TypeParam;

  auto noop = [] {
  };
  constant_unit_t<T> unit;
  auto f = semilift(noop, unit);

  static_assert(Equal<T, decltype(f())>::value, "");
  ASSERT_TRUE(equal(unit(), f()));
}

TEST(FunctionalMoveAssign, Basic) {
  using namespace ka;
  using M = move_only_t<int>;
  int const i = 3;
  M original{i};
  move_assign_t<M, M> move_assign{std::move(original)};
  M x{i + 1};
  move_assign(x); // x = std::move(original);
  ASSERT_EQ(i, *x);
}

TEST(FunctionalIncr, Regular) {
  using namespace ka;
  incr<int> incr;
  ASSERT_TRUE(is_regular({incr})); // only one possible value because no state
}

TEST(FunctionalIncr, Arithmetic) {
  using namespace ka;
  {
    incr<int> incr;
    int x = 0;
    incr(x);
    ASSERT_EQ(1, x);
  } {
    incr<double> incr;
    double x = 0.0;
    incr(x);
    ASSERT_EQ(1.0, x);
  }
}

TEST(FunctionalIncr, InputIterator) {
  using namespace ka;
  using namespace std;
  stringstream ss{"youpi les amis"};
  using I = istream_iterator<string>;
  I b(ss);
  incr<I> incr;
  ASSERT_EQ("youpi", *b);
  incr(b);
  ASSERT_EQ("les", *b);
  incr(b);
  ASSERT_EQ("amis", *b);
}

TEST(FunctionalDecr, Regular) {
  using namespace ka;
  decr<int> decr;
  ASSERT_TRUE(is_regular({decr})); // only one possible value because no state
}

TEST(FunctionalDecr, Arithmetic) {
  using namespace ka;
  {
    decr<int> decr;
    int x = 1;
    decr(x);
    ASSERT_EQ(0, x);
  } {
    decr<double> decr;
    double x = 1.0;
    decr(x);
    ASSERT_EQ(0.0, x);
  }
}

TEST(FunctionalDecr, BidirectionalIterator) {
  using namespace ka;
  using namespace std;
  decr<list<string>::iterator> decr;
  list<string> l{"youpi", "les", "amis"};
  auto b = end(l);
  decr(b);
  ASSERT_EQ("amis", *b);
  decr(b);
  ASSERT_EQ("les", *b);
  decr(b);
  ASSERT_EQ("youpi", *b);
}

TEST(FunctionalIncr, IsomorphicIntegral) {
  using namespace ka;
  {
    incr<int> incr;
    auto inv = retract(incr);
    int i = 0;
    incr(i);
    inv(i);
    ASSERT_EQ(0, i);
  } {
    incr<int> incr;
    auto inv = retract(incr);
    int i = 0;
    inv(i);
    incr(i);
    ASSERT_EQ(0, i);
  }
}

TEST(FunctionalIncr, IsomorphicBidirectionalIterator) {
  using namespace ka;
  using namespace std;
  incr<list<string>::iterator> incr;
  auto inv = retract(incr);
  list<string> l{"youpi", "les", "amis"};
  auto b = begin(l);
  ++b;
  incr(b);
  inv(b);
  ASSERT_EQ("les", *b);
  inv(b);
  incr(b);
  ASSERT_EQ("les", *b);
}

TEST(FunctionalDecr, IsomorphicIntegral) {
  using namespace ka;
  {
    decr<int> decr;
    auto inv = retract(decr);
    int i = 0;
    decr(i);
    inv(i);
    ASSERT_EQ(0, i);
  } {
    decr<int> decr;
    auto inv = retract(decr);
    int i = 0;
    inv(i);
    decr(i);
    ASSERT_EQ(0, i);
  }
}

TEST(FunctionalDecr, IsomorphicBidirectionalIterator) {
  using namespace ka;
  using namespace std;
  decr<list<string>::iterator> decr;
  auto inv = retract(decr);
  list<string> l{"youpi", "les", "amis"};
  auto b = begin(l);
  ++b;
  decr(b);
  inv(b);
  ASSERT_EQ("les", *b);
  inv(b);
  decr(b);
  ASSERT_EQ("les", *b);
}

TEST(FunctionalApply, Tuple) {
  using namespace ka;
  auto g = [](int i, char c, float f) {
    return std::make_tuple(i, c, f);
  };
  auto const args = std::make_tuple(5, 'a', 3.14f);
  ASSERT_EQ(args, apply(g, args));
  ASSERT_EQ(args, apply(g)(args));
}

TEST(FunctionalApply, Pair) {
  using namespace ka;
  auto g = [](int i, char c) {
    return std::make_pair(i, c);
  };
  auto const args = std::make_pair(5, 'a');
  ASSERT_EQ(args, apply(g, args));
  ASSERT_EQ(args, apply(g)(args));
}

TEST(FunctionalApply, Array) {
  using namespace ka;
  auto g = [](int i, int j, int k, int l) {
    return std::array<int, 4>{i, j, k, l};
  };
  std::array<int, 4> const args = {0, 1, 2, 3};
  ASSERT_EQ(args, apply(g, args));
  ASSERT_EQ(args, apply(g)(args));
}

TEST(FunctionalApply, Custom) {
  using namespace ka;
  using X = test::x_t<int, char, float>;
  auto g = [](int i, char c, float f) {
    return X{i, c, f};
  };
  X const args{5, 'a', 3.14f};
  ASSERT_EQ(args, apply(g, args));
  ASSERT_EQ(args, apply(g)(args));
}

TEST(FunctionalApply, MoveOnly) {
  using namespace ka;
  auto g = [](move_only_t<int> i, move_only_t<char> c, move_only_t<float> f) {
    return std::make_tuple(*i, *c, *f);
  };
  auto const res = std::make_tuple(5, 'a', 3.14f);
  {
    auto args = std::make_tuple(move_only_t<int>{5}, move_only_t<char>{'a'}, move_only_t<float>{3.14f});
    ASSERT_EQ(res, apply(g, std::move(args)));
  } {
    auto args = std::make_tuple(move_only_t<int>{5}, move_only_t<char>{'a'}, move_only_t<float>{3.14f});
    ASSERT_EQ(res, apply(g)(std::move(args)));
  }
}

TEST(FunctionalPolyIncr, Regular) {
  using namespace ka;
  ASSERT_TRUE(is_regular({poly_incr{}})); // only one possible value because no state
}

TEST(FunctionalPolyIncr, Basic) {
  using namespace ka;
  poly_incr incr;
  {
    int i = 0;
    incr(i);
    ASSERT_EQ(1, i);
  } {
    std::vector<int> v{1};
    auto b = begin(v);
    incr(b);
    ASSERT_EQ(end(v), b);
  }
}

TEST(FunctionalPolyDecr, Regular) {
  using namespace ka;
  ASSERT_TRUE(is_regular({poly_decr{}})); // only one possible value because no state
}

TEST(FunctionalPolyDecr, Basic) {
  using namespace ka;
  poly_decr decr;
  {
    int i = 1;
    decr(i);
    ASSERT_EQ(0, i);
  } {
    std::vector<int> v{1, 2};
    auto b = begin(v) + 1;
    decr(b);
    ASSERT_EQ(begin(v), b);
  }
}

TEST(FunctionalPolyIncr, Isomorphic) {
  using namespace ka;
  {
    poly_incr incr;
    auto decr = retract(incr);
    int i = 0;
    incr(i);
    decr(i);
    ASSERT_EQ(0, i);
  } {
    poly_decr decr;
    auto incr = retract(decr);
    int i = 0;
    decr(i);
    incr(i);
    ASSERT_EQ(0, i);
  }
}

TEST(FunctionalPolyIncr, Composition) {
  using namespace ka;
  using namespace ka::functional_ops;
  {
    poly_incr incr;
    auto incr_twice = (incr *= incr);
    int i = 0;
    incr_twice(i);
    ASSERT_EQ(2, i);
  } {
    poly_incr incr;
    auto decr = retract(incr);
    auto id = (incr *= decr *= decr *= incr);
    static_assert(traits::Equal<decltype(id), id_action>::value, "");
    int i = 0;
    id(i);
    ASSERT_EQ(0, i);
  }
}

struct trivial_scope_lockable_t {
  bool success;
  friend bool scopelock(trivial_scope_lockable_t& x) { return x.success; }
};

struct strict_scope_lockable_t {
  struct lock_t {
    ka::move_only_t<bool*> locked;

    lock_t(bool* l) : locked{ l } { **locked = true; }
    ~lock_t() { **locked = false; }

    lock_t(lock_t&& o) : locked{std::move(o.locked)} {}
    lock_t& operator=(lock_t&& o) { locked = std::move(o.locked); return *this; }

    explicit operator bool() const { return true; }
  };

  friend lock_t scopelock(strict_scope_lockable_t& l) {
    return lock_t{ l.locked };
  }

  bool* locked;
};

TEST(FunctionalScopeLock, ReturnsVoidSuccess) {
  using namespace ka;
  using L = trivial_scope_lockable_t;

  bool called = false;
  // TODO: pass by value instead of mutable store when source is available
  auto proc = scope_lock_proc([&]{ called = true; }, mutable_store(L{ true }));
  proc();
  ASSERT_TRUE(called);
}

TEST(FunctionalScopeLock, ReturnsVoidFailure) {
  using namespace ka;
  using L = trivial_scope_lockable_t;

  bool called = false;
  // TODO: pass by value instead of mutable store when source is available
  auto proc = scope_lock_proc([&]{ called = true; }, mutable_store(L{ false }));
  proc();
  ASSERT_FALSE(called);
}

TEST(FunctionalScopeLock, ReturnsProcResultOnLockSuccess) {
  using namespace ka;
  using L = trivial_scope_lockable_t;

  // TODO: pass by value instead of mutable store when source is available
  auto proc = scope_lock_proc([](int i){ return i + 10; }, mutable_store(L{ true }));
  auto res = proc(5);
  ASSERT_TRUE(res);
  ASSERT_EQ(15, res.value());
}

TEST(FunctionalScopeLock, ReturnsEmptyOptionalOnLockFailure) {
  using namespace ka;
  using L = trivial_scope_lockable_t;

  // TODO: pass by value instead of mutable store when source is available
  auto proc = scope_lock_proc([](int i){ return i + 10; }, mutable_store(L{ false }));
  auto res = proc(12);
  ASSERT_FALSE(res);
}

TEST(FunctionalScopeLock, StaysLockedUntilProcIsFinished) {
  using namespace ka;
  using L = strict_scope_lockable_t;

  bool locked = false;
  // TODO: pass by value instead of mutable store when source is available
  auto proc = scope_lock_proc(
      [&] {
        if (!locked)
          throw std::runtime_error("was not locked");
      },
      mutable_store(L{ &locked }));
  ASSERT_NO_THROW(proc());
  ASSERT_FALSE(locked);
}

using SharedPtrTypes = testing::Types<std::shared_ptr<int>,
                                      boost::shared_ptr<int>>;

template<typename T>
struct FunctionalScopeLockWeakPtr : testing::Test {
};

TYPED_TEST_CASE(FunctionalScopeLockWeakPtr, SharedPtrTypes);

TYPED_TEST(FunctionalScopeLockWeakPtr, SuccessfulLock) {
  using namespace ka;
  using ShPtr = TypeParam;

  ShPtr shptr{ new int{ 42 } };
  auto wkptr = weak_ptr(shptr);
  auto l = scopelock(wkptr);
  ASSERT_TRUE(l);
  ASSERT_EQ(2, l.use_count());
  ASSERT_EQ(shptr.get(), l.get());
}

TYPED_TEST(FunctionalScopeLockWeakPtr, FailureExpired) {
  using namespace ka;
  using ShPtr = TypeParam;

  ShPtr shptr;
  auto wkptr = weak_ptr(shptr);
  auto l = scopelock(wkptr);
  ASSERT_FALSE(l);
}

namespace {
  template<typename M>
  bool is_locked(M& m) {
     return std::async(std::launch::async, [&]{
       return !std::unique_lock<M>{ m, std::try_to_lock };
     }).get();
  }
}

using MutexTypes = testing::Types<std::mutex,
                                  std::recursive_mutex,
#if !BOOST_OS_ANDROID
                                  std::timed_mutex,
                                  std::recursive_timed_mutex,
#endif
                                  boost::mutex,
                                  boost::recursive_mutex,
                                  boost::timed_mutex,
                                  boost::recursive_timed_mutex,
                                  boost::shared_mutex>;

template<typename T>
struct FunctionalScopeLockMutexes : testing::Test {
};

TYPED_TEST_CASE(FunctionalScopeLockMutexes, MutexTypes);

TYPED_TEST(FunctionalScopeLockMutexes, Mutexes) {
  using namespace ka;
  using Mutex = TypeParam;

  Mutex m;
  ASSERT_FALSE(is_locked(m));
  {
    auto l = scopelock(m);
    ASSERT_TRUE(l);
    ASSERT_TRUE(is_locked(m));
  }
  ASSERT_FALSE(is_locked(m));
}