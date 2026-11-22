#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// init/used callbacks for the 16-bit buffer flavors: FML rides its
// own fbfr16 layout, VIEW is a raw C struct whose size comes from the
// view registry (VIEWFILES/VIEWDIR, the 16-bit names)

void fml16init(void *mem, size_t size);
void fml16reinit(void *mem, size_t size);
size_t fml16used(void *mem);
size_t view16used(void *mem);
size_t view32used(void *mem);
