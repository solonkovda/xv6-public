#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"
#include "fs.h"

#define MAX_SHEBANG_LENGTH 4096
#define MAX_SHEBANG_DEPTH 10


static int
exec_rec(char *path, char **argv, int shebang_depth)
{
  if (shebang_depth > MAX_SHEBANG_DEPTH) {
      end_op();
      cprintf("exec: fail\n");
      return -1;
  }
  
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;
  
  // Checking for shebang
  char buffer[2];
  if (readi(ip,buffer, 0, 2) != 2)
    goto bad;
  if (buffer[0] == '#' && buffer[1] == '!') {
    // Handling shebang
    char *shebang = kalloc();
    int sz = readi(ip, shebang, 2, MAX_SHEBANG_LENGTH);
    int found_last = 0;
    for (int i = 0; i < sz; i++) {
      if (shebang[i] == ' ' || shebang[i] == '\n') {
        shebang[i] = '\0';
        found_last = 1;
        break;
      }
    }
    if (!found_last) {
      kfree(shebang);
      goto bad;
    }
      
    iunlockput(ip);
    
    // Modifying argv
    char **new_argv = 0;
    if (shebang_depth == 0) {
      new_argv = (char**)kalloc();
      new_argv[0] = shebang;
      new_argv[1] = path;
      int i = 1;
      for (char **ptr = argv+1; *ptr; ptr++) {
        new_argv[++i] = *ptr;
      }
      new_argv[++i] = 0;
      argv = new_argv;
    }
    else {
      // In order to not allocate new memory buffer, we move all args to the
      // right.
      char **last;
      
      for (last = argv; *last; last++);
      *(last+1) = 0;
      for (; last != (argv+1); last--) {
        *last = *(last-1);
      }
      argv[0] = shebang;
      argv[1] = path;
    }
    end_op();
    int res = exec_rec(shebang, argv, shebang_depth + 1);
    kfree(shebang);
    if (shebang_depth == 0) 
      kfree((char*)new_argv);
    return res;
  }

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;
  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));
  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

int
exec(char *path, char **argv) {
  return exec_rec(path, argv, 0);
}
