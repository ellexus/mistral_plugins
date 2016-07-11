Custom Plug-in development man pages
====================================

This directory tree contains custom man pages for our standard functions. In
order to have the man command find these files the ``$MANPATH`` environment
variable must be set to point to this directory e.g. if your repository is held
in your home directory use::

    export MANPATH=${MANPATH}:${HOME}/mistral_plugins/man/

Note that the leading ``:`` is required even if ``$MANPATH`` is currently
undefined so ``man`` will continue to find the system man pages.
