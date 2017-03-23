//
// Created by LZephyr on 2017/3/21.
//

#ifndef LIBTOOLING_COMMONS_H
#define LIBTOOLING_COMMONS_H

enum CallGraphOption: unsigned {
    /// Only generate graph file
    O_GraphOnly = 0,

    /// Only generate dot file
    O_DotOnly,

    /// Generate both dot and graph file
    O_DotAndGraph
};

#endif //LIBTOOLING_COMMONS_H
