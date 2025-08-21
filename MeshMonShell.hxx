/*
 * MeshMonShell.hxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#ifndef MESHMONSHELL_HXX
#define MESHMONSHELL_HXX

#include <MeshShell.hxx>

using namespace std;

class MeshMonShell : public MeshShell {

public:

    MeshMonShell(shared_ptr<MeshClient> client = NULL);
    ~MeshMonShell();

protected:

    virtual shared_ptr<MeshShell> newInstance(void);
    virtual int system(int argc, char **argv);

};

#endif

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
