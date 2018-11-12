//////////////////////////////////////////////////////////////////////
//                      North Carolina State University
//
//
//
//                             Copyright 2018
//
////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
////////////////////////////////////////////////////////////////////////
//
//   Author:  Hung-Wei Tseng, Yu-Chia Liu
//
//   Description:
//     Core of Kernel Module for Processor Container
//
////////////////////////////////////////////////////////////////////////

// Project 2: Sujal Sujal, ssujal; Bhanu Sreekar Reddy Karumuri, bkarumu 

#include "memory_container.h"

#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/kthread.h>

struct object
{
	__u64 oid;
	struct object* next;
	unsigned long long objspace;
};

struct task
{
	struct task_struct* thread;
	struct task* next;
};

struct container
{
	__u64 cid;		
	int task_cnt;
	struct task* task_list;
	struct container* next;
	struct object* obj;
};

static struct container* ctr_list = NULL;  //list of containers

DEFINE_MUTEX(my_mutex); //working with global lock  

struct container* getContainerFromCid(__u64 cid)
{
	struct container* ctrNode;

	if(ctr_list == NULL)
	{
		return NULL;
	}
	ctrNode = ctr_list;
	
	while(ctrNode!=NULL)
	{
		if(ctrNode->cid == cid)
		{
			//printk("Container Found %llu \n", ctrNode->cid);
			break;
		}
		ctrNode = ctrNode->next;
	}
	return ctrNode;
}

struct task* getNewTask( void )
{
	struct task* tn = NULL;
	tn = (struct task*)kmalloc(sizeof(struct task), GFP_KERNEL);

	if(tn == NULL)
	{
		return NULL;
	}

	tn->next = NULL;
	tn->thread = current;
	return tn;
}

struct container* getNewContainer(__u64 cid)
{
	struct container* ctrNode = NULL;
	ctrNode = (struct container*)kmalloc(sizeof(struct container), GFP_KERNEL);
	
	if(ctrNode == NULL)
	{
		//printk("Unable to create a container...\n");
		return NULL;
	}
	ctrNode->task_cnt = 1;
	ctrNode->cid = cid;
	ctrNode->next = NULL;
	ctrNode->task_list = NULL;
	ctrNode->obj = NULL;
		
	return ctrNode; 
}

// getting container id of the current task //
struct container* getContainer(pid_t pid)
{
	//printk("Finding the CID of current pid %d...... \n", pid);
	
	struct container* ctrNode;

	if(ctr_list == NULL)
	{
		//printk("Container not found\n");
		return NULL;
	}
	ctrNode = ctr_list;
	
	struct task * tmp = NULL;

	while(ctrNode != NULL)
	{
		tmp = ctrNode->task_list;
		while(tmp!=NULL)
		{
			if(tmp->thread->pid == pid)
			{
				//printk("Container found for current pid....%llu \n", ctrNode->cid);		
				return ctrNode;
			}
			tmp= tmp->next;		
		}
		ctrNode= ctrNode->next;			
	}
	return ctrNode;
}

// Memory-Mapping function
int memory_container_mmap(struct file *filp, struct vm_area_struct *vma)
{
	//printk("\nmmap called..... \n");

	__u64 oid = vma->vm_pgoff;

	struct container* ctrNode = getContainer(current->pid);

	if(ctrNode == NULL)
	{
		//printk("Container not found...!!! \n");
	}
	else
	{
		//printk("Container found inside mmap....\n");
		//for a given container

		//create object reference
		struct object* temp = ctrNode->obj;
	
		bool flag = false;

		if(temp == NULL)
		{
			//create a new memory object
			struct object* con_obj = kcalloc(1,sizeof(struct object), GFP_KERNEL);
			con_obj->oid = oid;
			con_obj->next = NULL;
			con_obj->objspace = kcalloc(1,vma->vm_end - vma->vm_start, GFP_KERNEL);
	
			//printk("Creating first memory object of container with cid %llu and object id %llu \n", ctrNode->cid, oid);
			
			//link this created object to the specific container
			ctrNode->obj = con_obj;
			temp = ctrNode->obj;
		}
		else if(temp->oid != oid)
		{
			//if there exists only one object
			while(temp->next != NULL)
			{
				if(temp->next->oid == oid)
				{
					temp= temp->next;
					flag = true;
					//printk("Object for container with cid %llu and object id %llu \n", ctrNode->cid, oid);
					break;
				}
				temp = temp->next;
			}
			if(!flag)
			{
				//if no object already exists, create one
				//printk("Creating object for container with cid %llu and object id %llu \n", ctrNode->cid, oid);

				struct object* con_obj = kcalloc(1,sizeof(struct object), GFP_KERNEL);
				con_obj->oid = oid;
				con_obj->next = NULL;
				con_obj->objspace = kcalloc(1,vma->vm_end - vma->vm_start, GFP_KERNEL);

				temp->next = con_obj;
				temp = temp->next;
			}
		}
		else
		{
			//Referring the first object
			//printk("Object already exists! And the first one too!");
		}
		
		unsigned long long *k_malloc;

		k_malloc = PAGE_ALIGN(temp->objspace);
		
		unsigned long pfn = virt_to_phys(temp->objspace)>>PAGE_SHIFT;

		//printk("Physical Mem location .... %x ", pfn);

		unsigned long long ans;

		ans = remap_pfn_range(vma, vma->vm_start, pfn, vma->vm_end - vma->vm_start, vma->vm_page_prot);
		
		if(ans < 0)
		{
			//printk("Sorry! could not map the address.... \n");
			return -EIO;
		} 
	}

    return 0;
}


//Locking function
int memory_container_lock(struct memory_container_cmd __user *user_cmd)
{
	//printk("\nLocking..... \n");	
	mutex_lock(&my_mutex);
    return 0;
}

//Unlocking function
int memory_container_unlock(struct memory_container_cmd __user *user_cmd)
{
	//printk("\nUnlocking..... \n");	
	mutex_unlock(&my_mutex);
    return 0;
}

//Deletion function
int memory_container_delete(struct memory_container_cmd __user *user_cmd)
{
	struct memory_container_cmd ctrCmd;
    	struct container* ctrNode = NULL;
    	struct task* temp = NULL;
    	struct task* prev = NULL;

    	copy_from_user(&ctrCmd, user_cmd, sizeof(struct memory_container_cmd));
	//printk("\n\n");
    	//printk( "try to take lock %d \n",current->pid);
    	mutex_lock(&my_mutex);
    	//printk( "Attained lock %d \n",current->pid);
    
    	//printk( "Delete called Container with cid = %llu \n",ctrCmd.cid);
	
	//get the container for current running process
	ctrNode = getContainer(current->pid);	
	
    	if(ctrNode == NULL )
	{
        	//printk( "Container not found %llu \n",ctrNode->cid);
        	//printk( "releasing lock %d",current->pid);
        	mutex_unlock(&my_mutex);
        	return 0;
    	}
      	//printk( "Delete: Container found %llu \n",ctrNode->cid);
    
    	if(ctrNode->task_list == NULL )
	{
        	//printk( "Container has no tasks %llu \n",ctrNode->cid);
	        //printk( "releasing lock %d \n",current->pid);
	        mutex_unlock(&my_mutex);
	        return 0;
	}

    	if (ctrNode->task_list->next == NULL)
	{
        	// only 1 task in container, delete only the task and not the container
        	//printk( "Delete: only 1 task in container %llu \n",ctrNode->cid);
        	ctrNode->task_cnt = 0;
		temp = ctrNode->task_list;
		ctrNode->task_list = NULL;
        	kfree(temp);
        	//printk( "releasing lock %d \n",current->pid);
        	mutex_unlock(&my_mutex);
        	return 0;
    	}
	else 
	{
        	// first task to be deleted
        	//printk( "Delete: delete a task %llu pid= %d\n",ctrNode->cid, current->pid);
        	if (current == ctrNode->task_list->thread)
		{
            		//printk( "Delete: first task to be deleted %llu \n",ctrNode->cid);
            
            		temp = ctrNode->task_list;
            		ctrNode->task_list = ctrNode->task_list->next;
            		ctrNode->task_cnt -=1;
            		kfree(temp);
        	}
		else
		{
            		//printk( "Delete: not the first task to be deleted %llu \n",ctrNode->cid);
            		temp = ctrNode->task_list;
            		prev = temp;
            		while(temp != NULL && temp->thread != current)
			{
	                	prev = temp;
	                	temp = temp->next;
	            	}
	            //printk( "Delete: still here!!%llu \n",ctrNode->cid);

            		if (temp!= NULL && temp->next == NULL )
			{	
        	        	//printk( "Delete: last node to be deleted  %llu \n",ctrNode->cid);
        	        	// if temp is last node
        	        	ctrNode->task_cnt -=1;
        	        	prev->next = NULL;
        	        	kfree(temp);
     	  		}
			else if (temp!=NULL && temp->next != NULL)
			{
                		//printk( "Delete: not the last node to be deleted  %llu \n",ctrNode->cid);
                		ctrNode->task_cnt -=1;
                		prev->next = temp->next;
                		kfree(temp);
            		}
			else
			{
                		//just-in-case 
        			//printk( "releasing lock %d \n",current->pid);
                		mutex_unlock(&my_mutex);
                		return 0;
            		}
        	}
        
    }    
	//printk( "releasing lock %d \n",current->pid);
	mutex_unlock(&my_mutex);
    return 0;
}

//Creation function
int memory_container_create(struct memory_container_cmd __user *user_cmd)
{
	struct container* ctrNode = NULL;
	struct task* tn= NULL;
	struct task* temp= NULL;


	struct memory_container_cmd ctrCmd;
	copy_from_user(&ctrCmd, user_cmd, sizeof(struct memory_container_cmd));
	
	//printk("Try to take the lock %d \n", current->pid);
	mutex_lock(&my_mutex);
	//printk("Attained lock %d \n", current->pid);

	ctrNode = getContainerFromCid(ctrCmd.cid);
	
	if(ctrNode != NULL)
	{
		//printk("Container exists %llu \n", ctrNode->cid);

		tn = getNewTask();
		//printk("\n after getNewtask!\n");
		
		if(tn == NULL)
		{
			//printk("releasing the lock, task not created %d \n", current->pid);
			mutex_unlock(&my_mutex);
			return 0;
		}

		ctrNode->task_cnt +=1;

		//Adding task to the list

		if(ctrNode->task_list == NULL)
		{
			//printk("tasks added at start.\n");
			ctrNode->task_list = tn;
		}
		else
		{
			//printk("task added at last.\n");
			tn->next = ctrNode->task_list;
			ctrNode->task_list = tn;			
		}	
	}
	else
	{
		//printk("Container Not Found %llu, creating a new container... \n", ctrCmd.cid);
		ctrNode = getNewContainer(ctrCmd.cid);

		if(ctrNode == NULL)
		{
			//printk("getNewContainer failed %llu \n", ctrCmd.cid);
			//printk("releasing lock %d", current->pid);
			mutex_unlock(&my_mutex);
			return 0;
		}
	
		//printk("creating new task for the container.... %llu \n", ctrCmd.cid);
		
		tn = getNewTask();

		if(tn == NULL)
		{
			//printk("getNewTask Failed %llu... \n", ctrCmd.cid);
			//printk("releasing the lock.... %d", current->pid);
			mutex_unlock(&my_mutex);
			return 0;
		}
		
		ctrNode->task_list = tn;

		if(ctr_list ==  NULL)
		{
			//printk("Adding the first container %llu..... \n", ctrCmd.cid);
			ctr_list = ctrNode;
		}
		else
		{
			//printk("Adding additional container %llu..... \n", ctrCmd.cid);
			ctrNode->next = ctr_list;
			ctr_list = ctrNode;
		}
	}
	//printk("releasing the lock %d", current->pid);
	mutex_unlock(&my_mutex);

    return 0;
}


//Memory free function
int memory_container_free(struct memory_container_cmd __user *user_cmd)
{
	struct memory_container_cmd ctrCmd;	

	
	copy_from_user(&ctrCmd, user_cmd, sizeof(struct memory_container_cmd));

	//printk("Inside free().... \n");
	
	struct container* ctrNode = getContainer(current->pid);

	//printk("Container id inside free()=%llu \n", ctrNode->cid);

	if(ctrNode == NULL)	
	{
		//printk("No Container exists... \n");
	}
	else
	{
		//printk("Memory free for container id %llu and object id %llu = \n",ctrNode->cid, ctrNode->obj->oid);

		struct object* temp_ref = ctrNode->obj;
		
		//printk("CMD oid= %llu and container oid= %llu \n", ctrCmd.oid, temp_ref->oid);

		if(temp_ref == NULL)
		{
			//printk("No object to free \n");
		}
		
		//Deleting first object
		if(temp_ref->oid == ctrCmd.oid)
		{
			if(temp_ref->next == NULL)
			{
				//printk("Only object of the container.. \n");
				ctrNode->obj = NULL;
				kfree(temp_ref); //free the temp pointer
				//printk("Freed only object  \n");
			}
			else
			{
				//printk("First object of the container.... \n");
				ctrNode->obj = temp_ref->next;
				kfree(temp_ref);
				//printk("Freed first object  \n");
			}
		}
		else
		{
			//printk("Middle object of the container ... \n");

			struct object *prev;

			while((temp_ref->oid != ctrCmd.oid)&&(temp_ref->next != NULL))	
			{
				prev = temp_ref;
				temp_ref = temp_ref->next;
			}
			
			if(temp_ref->oid == ctrCmd.oid)
			{
				prev->next = temp_ref->next;
				kfree(temp_ref);
				//printk("Freed middle object  \n");
			}
			else
			{
				//printk("Sorry, No object found!! \n");
			}
		}
	}	
    return 0;
}

/**
 * control function that receive the command in user space and pass arguments to
 * corresponding functions.
 */
int memory_container_ioctl(struct file *filp, unsigned int cmd,
                              unsigned long arg)
{
    switch (cmd)
    {
    case MCONTAINER_IOCTL_CREATE:
        return memory_container_create((void __user *)arg);
    case MCONTAINER_IOCTL_DELETE:
        return memory_container_delete((void __user *)arg);
    case MCONTAINER_IOCTL_LOCK:
        return memory_container_lock((void __user *)arg);
    case MCONTAINER_IOCTL_UNLOCK:
        return memory_container_unlock((void __user *)arg);
    case MCONTAINER_IOCTL_FREE:
        return memory_container_free((void __user *)arg);
    default:
        return -ENOTTY;
    }
}
