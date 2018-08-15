This repository uses subtrees to manage some of its externals.

## Initial setup

```
git remote add externals-fmt https://github.com/fmtlib/fmt.git
git remote add externals-xbyak https://github.com/herumi/xbyak.git
```

## Updating

Change `<ref>` to refer to the appropriate git reference.

```
git fetch externals-fmt
git fetch externals-xbyak
git subtree pull --squash --prefix=externals/fmt externals-fmt <ref>
git subtree pull --squash --prefix=externals/xbyak externals-xbyak <ref>
```
