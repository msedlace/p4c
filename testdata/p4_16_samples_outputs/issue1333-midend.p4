#include <core.p4>

extern void f(bit<32> a=32w0, bit<32> b);
extern E {
    E(bit<32> x=32w0);
    void f(in bit<16> z=16w2);
}

control ctrl();
package top(ctrl _c);
control c_0() {
    @name("c_0.e") E(x = 32w0) e;
    @hidden action act() {
        f(a = 32w0, b = 32w4);
        e.f(z = 16w2);
    }
    @hidden table tbl_act {
        actions = {
            act();
        }
        const default_action = act();
    }
    apply {
        tbl_act.apply();
    }
}

top(c_0()) main;

