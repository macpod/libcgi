/*
    LibCGI - A library to make CGI programs using C
    Copyright (C) 2001 Rafael Steil

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    You can contact the author by e-mail: rafael@insanecorp.com
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cgi.h"
#include "error.h"

// Whow... if hextable array has a length less than 256,
// the cgi_unescape_special_chars function will fail.  And I don't know why
static int hextable[256];

int headers_initialized = 0;

formvars *formvars_start = NULL;
formvars *formvars_last = NULL;
/* FILES */
formvarsFiles *formvars_start_files = NULL;
formvarsFiles *formvars_last_files = NULL;

// session.c
extern formvars *sess_list_start;

// Set to 1 to activate runtime debugation, 0 to disable it
int cgi_display_errors = 1;

// cookie.c
extern formvars *cookie_start;
extern formvars *cookie_end;

// Separates *query in name=value pairs, then gets each piece of result of them, storing
// the result in the linked list global variable
formvars *process_data(char *query, formvars **start, formvars **last, const char delim, const char sep)
{
	register size_t position = 0, total_len = 0, i = 0;
	char *aux, *str_unesc;
	formvars *data;

	if (query == NULL)
		return *start;

	total_len = strlen(query);
	aux = query;
	while (*query) {
		position = 0;

		data = (formvars *)malloc(sizeof(formvars));
		if (!data)
			libcgi_error(E_MEMORY, "%s, line %s", __FILE__, __LINE__);

		memset(data, 0, sizeof(formvars));

		// Scans the string for the next 'delim' character
		while (*aux && (*aux != delim)) {
			position++;
			aux++;
			i++;
		}

		if (*aux) {
			aux++;
			i++;
		}

		data->name = (char *)malloc(position+1);
		if (data->name == NULL)
			libcgi_error(E_MEMORY, "%s, line %s", __FILE__, __LINE__);

		strncpy(data->name, query, position);
		data->name[position] = '\0';

		query = aux;
		position = 0;
		while (*aux && (*aux != sep)) {
			if ((*aux == '%') && (i + 2 <= total_len)) {
				if (isalnum(aux[1]) && isalnum(aux[2])) {
					aux += 2;
					i += 2;
					position++;
				}
			}
			else
				position++;

			aux++;
			i++;
		}

		if (*aux) {
			aux++;
			i++;
		}

		data->value = (char *)malloc(position+1);
		if (data->value == NULL)
			libcgi_error(E_MEMORY, "%s, line %s", __FILE__, __LINE__);

		str_unesc = cgi_unescape_special_chars(query);
		strncpy(data->value, str_unesc, position);
		data->value[position] = '\0';
		free(str_unesc);

		slist_add(data, start, last);

		query = aux;
	}

	return *start;
}

/*****************************************************
					CGI GROUP
*****************************************************/
/** @defgroup libcgi_cgi CGI manipulation
* @{
*/

/**
* Process HTML form or URL data.
* Used to retrieve GET or POST data. It handles automaticaly the correct REQUEST_METHOD, so you don't need to afraid about it.
* @return Returns the contents of URL or FORM into a formvars variable, or NULL if FALSE. Most of time, you
* don't need any variable to store the form data, because is used an internal variable to manipulate the contents.
* @see cgi_init, cgi_init_headers
**/
formvars *cgi_process_form()
{
	char *method;

	method = getenv("REQUEST_METHOD");

	// When METHOD has no contents, the default action is to process it as GET method
	if (method == NULL || !strcasecmp("GET", method)) {
		char *dados;
		dados =	getenv("QUERY_STRING");

		// Sometimes, GET comes with not any data
		if (dados == NULL || strlen(dados) < 1)
			return NULL;

		return process_data(dados, &formvars_start, &formvars_last, '=', '&');
	}
	else if (!strcasecmp("POST", method)) {
		char *post_data;
		char *tmp_data;
		int content_length;
		formvars *ret;

		tmp_data = getenv("CONTENT_LENGTH");
		if (tmp_data == NULL)
			return NULL;

		content_length = atoi(tmp_data);
		/* FILE */
		if(strstr(getenv("CONTENT_TYPE"),"multipart/form-data")!=NULL)
		{
			if( cgi_param_files() == NULL )
				return NULL;
			return formvars_start;
		}
		else
		{
			post_data = (char *)malloc(content_length + 1);
			if (post_data == NULL)
				libcgi_error(E_MEMORY, "%s, line %s", __FILE__, __LINE__);

			fread(post_data, content_length, 1, stdin);
			post_data[content_length] = '\0';

		    ret = process_data(post_data, &formvars_start, &formvars_last, '=', '&');
		    free(post_data);
		    return ret;
		}
	}

	return NULL;
}
/**
 * FILES
 * 
 * 
 */
formvarsFiles *cgi_param_files()
{
	/* FILES */
	char *separate = (char *)malloc(100*sizeof(char));
	strcpy(separate,"--");
	
	char buf[BUFSIZ];
	
	char *p = strdup(getenv("CONTENT_TYPE"));	
	
	p = strstr(p,"=");
		
	strcat(separate,++p);	
	int len = strlen(separate);
	strcat(separate,"--");	
	
	formvarsFiles *file_nodo = NULL;
	formvars *p_nodo = NULL;			
	
	int flag_filename = 0;
	int flag_name = 0;
		
	while(!feof(stdin))
	{
		if(!flag_filename)
		{
			if(!fgets(buf,BUFSIZ,stdin)) 			
				return NULL;
		}
		else
		{
			char c;
			int i = 0;
			size_t size = 0;
			memset(buf,0,BUFSIZ);
			while(!feof(stdin))
			{
				if(fread(&c,sizeof(char),1,stdin)>0){
					buf[i] = c;
					i++;
					if (i >= BUFSIZ) {
						size += fwrite(buf, sizeof(char), BUFSIZ, formvars_last_files->fp);
						i = 0;
					}
					else if (c == '\012'){
						buf[i]	= '\0';
						if (strncmp(buf, separate, len) == 0) {
							(void)fseek(formvars_last_files->fp, 0L, SEEK_SET);
							/* remove CRLF in last line */
							ftruncate(fileno(formvars_last_files->fp), (off_t)(size - 2));
							break;
						}
						size += fwrite(buf, sizeof(char), (size_t)i, formvars_last_files->fp);
						i = 0;
					}
				} 
			}
			flag_filename = 0;			
		}
		if(!strncmp(buf,separate,len+2))		
			return formvars_start_files;
					
		if(!strncmp(buf,separate,len))
		{
			if(!fgets(buf,BUFSIZ,stdin))
				return NULL;
			if(strstr(buf,"filename=")!=NULL)
			{
				flag_filename = 1;
				flag_name = 0;						
				file_nodo = (formvarsFiles*)malloc(sizeof(formvarsFiles));
				if(!file_nodo)
				{
					return NULL;
				}
				/* name */
				char *t1 = (char *)malloc(100*sizeof(char));
				
				strcpy(t1,strstr(buf," name=")+7);
				
				int i;
				for(i=0;t1[i]!='"';i++){}
				t1[i] = '\0';				
				trim(t1);				
				file_nodo->name = t1;
				
				/* value */
				char *t2 = (char *)malloc(100*sizeof(char));
				
				strcpy(t2,strstr(buf,"filename=")+10);				
				
				for(i=0;t2[i]!='"';i++){}
				t2[i] = '\0';				
				trim(t2);				
				file_nodo->value = t2;
									
				file_nodo->next = NULL;
				FILE *ftmp = tmpfile();	
				file_nodo->fp = ftmp;
				if(!file_nodo->fp)
				{						
					return NULL;
				}
				if(formvars_start_files == NULL)
				{
					formvars_start_files = file_nodo;
					formvars_last_files = file_nodo;
				}
				else							
				{
					formvars_last_files->next = file_nodo;
					formvars_last_files = file_nodo;
				}
				if(!fgets(buf,BUFSIZ,stdin)) return NULL;
				if(!fgets(buf,BUFSIZ,stdin)) return NULL;							
			}
			else if(strstr(buf,"name="))
			{
				flag_filename = 0;
				flag_name = 1;
				char *t = (char *)malloc(100*sizeof(char));
				strcpy(t, strstr(buf, "name=") + 6);				
				int i;
				for(i=0;t[i]!='"';i++){}
				t[i] = '\0';
				trim(t);					
				p_nodo = (formvars * )malloc(1*sizeof(formvars));
				p_nodo->name = t;
				p_nodo->next = NULL;
				if(!formvars_start)
				{
					formvars_start = p_nodo;
					formvars_last = p_nodo;
				}
				else
				{
					formvars_last->next = p_nodo;
					formvars_last = p_nodo;
				}
				if(!fgets(buf,BUFSIZ,stdin)) 
					return NULL;
			}
		}
		else
		{
			if(flag_name)
			{
				formvars_last->value = strdup(buf);
				trim(formvars_last->value);
			}
		}
	}
	return formvars_start_files;
}
 
/**
* Kills the application with a message.
* Writes msg and terminate
* @param msg Message to send to the browser before killing
*/
void cgi_fatal(const char *msg)
{
	cgi_init_headers();

	puts(msg);

	cgi_end();
	exit(1);
}

/**
* Include static files.
* Function used to include static data ( normaly html files ).
* File contents will never be processed.
* Note that I don't scan for any special character. The reason
* I did it is, if the you are using this library, you have a shell
* where you can compile the cgi program. And can do much more ;-)
* @param filename Filename with full path to include
* @return If an error occurs and libcgi_debug is true, then a warning
* message is showed.
* @see libcgi_debug
*
* \code
* cgi_include("top_bar.htm");
* \endcode
*/
int cgi_include(const char *filename)
{
	FILE *inc;
	char buffer[255];

	if (!(inc = fopen(filename, "r"))) {
		cgi_init_headers();

		libcgi_error(E_WARNING, "Failed to open include file <b>%s</b>", filename);

		return 0;
	}

 	while (fgets(buffer, 255, inc))
		printf("%s", buffer);

	fclose(inc);

	return 1;
}

/**
* Initialize HTML headers.
* You need to call this function before that any content is send to the brosert, otherwise you'll get an error (Error 500).
* @see cgi_init
**/
void cgi_init_headers()
{
	if (!headers_initialized) {
		printf("Content-type: text/html\r\n\r\n");

		headers_initialized = 1;
	}
}

/**
* Return all values with the same name sent by a form.
* @param name Form variable name
* @return Form variable contents
* @see cgi_param
*
* Example:
* For example, if in your HTML you have something like<br>
*  <br>
* <pre>
* "What do you like??"<br>
*  Computers : &lt;input type='checkbox' name='like' value='computers'&gt;&lt;br&gt;
*  Internet : &lt;input type='checkbox' name='like' value='net'&gt;&lt;br&gt;
*  games : &lt;input type='checkbox' name='like' 'value='games''&gt;&lt;br&gt;
* </pre>
*       <br>
* then, to retrieve all values, you can make a code like<br><br>
*
* \code
* // ...
* char *data;
* \\ ...
* while ((data = cgi_param_multiple("like")) != NULL)
* 	puts(data);
* \\ ...
* \endcode
**/
char *cgi_param_multiple(const char *name)
{
	static unsigned int counter = 0;
	unsigned int pos = 0;
	formvars *begin;

	begin = formvars_start;

	while (begin) {
		if ((!strcasecmp(begin->name, name)) && (++pos > counter)) {
			counter++;

			return (begin->value);
		}

		begin = begin->next;
	}

	counter = 0;

	return NULL;
}

/**
*  Recirects to the specified url.
* Remember that you cannot send any header before this function, or it will not work.
* <b>Note:</b><br>
* LibCGI does not implement RFC 2396 to make the lib simple and quick. You should be sure
* to pass a correct URI to this function.
* @param url url to redirect the browser
*
* \code
* cgi_redirect("http://wwww.linux.org");
* \endcode
**/
void cgi_redirect(char *url)
{
	if (headers_initialized) {
		libcgi_error(E_WARNING, "<br><b>Cannot redirect. Headers already sent</b><br>");

		return;
	}

	printf("Location: %s\r\n\r\n", url);
}

// Original idea from cgic library
void init_hex_table()
{
	memset(hextable, 0, 255);

	hextable['1'] = 1;
	hextable['2'] = 2;
	hextable['3'] = 3;
	hextable['4'] = 4;
	hextable['5'] = 5;
	hextable['6'] = 6;
	hextable['7'] = 7;
	hextable['8'] = 8;
	hextable['9'] = 9;
	hextable['a'] = 10;
	hextable['b'] = 11;
	hextable['c'] = 12;
	hextable['d'] = 13;
	hextable['e'] = 14;
	hextable['f'] = 15;
	hextable['A'] = 10;
	hextable['B'] = 11;
	hextable['C'] = 12;
	hextable['D'] = 13;
	hextable['E'] = 14;
	hextable['F'] = 15;
}

/**
*  Main cgi function.
*  Configures all (most?) we need to  get cgi library working correctly. It MUST be called before
*  any other cgi function.
*  @see cgi_end, cgi_process_form, cgi_init_headers
**/
int cgi_init()
{
	init_hex_table();

	// Well... the reason I put cgi_get_cookies() here is to not
	// cause problems with session's. Note that, when you want
	// to use session within your program, you need  cgi_get_cookies()
	// before session_start(), otherwise we will get some problems... :)
	// Calling this function here is the best way. Trust me :)
	cgi_get_cookies();

	return 1;
}

/**
* Performs cgi clean ups.
* Provides some methods to clean memory or any other job that need to be done before the end of the application.
* @see cgi_init
**/
void cgi_end()
{
	slist_free(&formvars_start);

	formvars_last = NULL;

	if (sess_list_start)
		slist_free(&sess_list_start);

	if (cookies_start)
		slist_free(&cookies_start);
}

/**
* Transforms' URL special chars.
* Search for special chars ( like %%E1 ) in str, converting them to the ascii character correspondent.
* @param str String containing data to parse
* @return The new string
* @see cgi_escape_special_chars
**/
char *cgi_unescape_special_chars(char *str)
{
	char *tmp;
	register int i, len, pos = 0;

	len = strlen(str);
	tmp = (char *)malloc(len + 1);
	if (tmp == NULL)
		libcgi_error(E_MEMORY, "%s, line %s", __FILE__, __LINE__);

	for (i = 0; i < len; i++) {
		// If we found a '%' character, then the next two are the character
		// hexa code. Converting a hexadecimal code to their decimal is easy:
		// The first character needs to be multiplied by 16 ( << 4 ), and the another
		// one we just get the value from hextable variable
		if ((str[i] == '%') && isalnum(str[i+1]) && isalnum(str[i+2])) {
			tmp[pos] = (hextable[(unsigned char) str[i+1]] << 4) + hextable[(unsigned char) str[i+2]];
			i += 2;
		}
		else if (str[i] == '+')
			tmp[pos] = ' ';
		else
			tmp[pos] = str[i];

		pos++;
	}

	tmp[pos] = '\0';

	return tmp;
}

/**
* Transforms' special characters into hexadecimal form ( %%E1 ).
* @param str String to parse
* @return The new string
* @see cgi_unescape_special_chars
**/
char *cgi_escape_special_chars(char *str)
{
	unsigned char hex[] = "0123456789ABCDEF";
	register int i, j, len, tmp_len;
	unsigned char *tmp;

	len = strlen(str);
	tmp_len = len;
	tmp = (unsigned char*)malloc(len+1);
	if (tmp == NULL)
		libcgi_error(E_MEMORY, "%s, line %s", __FILE__, __LINE__);

	for (i = 0, j = 0; i < len; i++, j++) {
		tmp[j] = (unsigned char)str[i];
		if (tmp[j] == ' ')
			tmp[j] = '+';
		else if (!isalnum(tmp[j]) && strchr("_-.", tmp[j]) == NULL) {
			tmp_len += 3;
			tmp = realloc(tmp, tmp_len);
			if (!tmp)
				libcgi_error(E_MEMORY, "%s, line %s", __FILE__, __LINE__);

			tmp[j++] = '%';
			tmp[j++] = hex[(unsigned char)str[i] >> 4];
			tmp[j] = hex[(unsigned char)str[i] & 0x0F];
		}
	}

	tmp[j] = '\0';

	return tmp;
}

/**
* Gets the of HTML or URL variable indicated by 'name'
* @param name Form Variable name
* @see cgi_param_multiple,  cgi_process_form, cgi_init
*
* \code
* // ...
* char *contents;
*
* cgi_init();
* cgi_process_form();
* cgi_init_headers();
*
* contents = cgi_param("foo");
*
* puts(contents);
*
* // ...
* \endcode
**/
char *cgi_param(const char *var_name)
{
	return slist_item(var_name, formvars_start);
}
/**
 * FILES
 * 
 * 
 * 
 * 
 */
char * cgi_files_filename(const char *var_name)
{
	return slist_value_files(var_name,formvars_start_files);
}

/*
Saves a file. This needs to be rewritten.
The data is saved to the specified file name. If the file name ends with a /
it is assumed to be a directory and the uploaded file's name is used.
*/
int cgi_files_save(const char *var_name, const char *filename)
{
	FILE *fin = slist_item_files(var_name,formvars_start_files);
	FILE *fout;
	char *tmp = NULL;
    int len;

	if(fin == NULL || filename == NULL || 
        cgi_files_filename(var_name) == NULL)
		return -2;	

    len = strlen(filename);
    if (filename < 1) {
        return -3;
    }
	
	if (filename[len-1] == '/') { // This is a directory
    	tmp = malloc(len+sizeof(cgi_files_filename(var_name))+1);
    	if (!tmp) {
			libcgi_error(E_MEMORY, "%s, line %s", __FILE__, __LINE__);
	    }	    

    	strcpy(tmp,filename);
    	strcat(tmp,cgi_files_filename(var_name));
        fout = fopen(tmp, "w");
        free(tmp);

	} else { // This should be a file/
    	fout = fopen(filename, "w");	
	}
	

	if (fout == NULL) {
		libcgi_error(E_FATAL, "%s, line %s", __FILE__, __LINE__);
		return -1;
	}

	while (!feof(fin)) {
		size_t	len;
		char	buf[BUFSIZ];

		len = fread(buf, sizeof(char), sizeof(buf), fin);
		fwrite(buf, sizeof(char), len, fout);
	}
	fclose(fout);
	
	(void)fseek(fin, 0L, SEEK_SET);	
	return 1;
}
/** END FILES **/
/**
* Sends a specific header.
* Sends a specific HTTP header. You won't need to add '\\n\\n' chars.
* @param header HTTP header to send, without new line characteres
* @return True
* @see cgi_init_headers
**/
void cgi_send_header(const char *header)
{
	printf("%s\r\n", header);
}


/**
* @}
*/
